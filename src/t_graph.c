#include "server.h"
#include "t_graph.h"
#include <math.h> /* isnan(), isinf() */

robj *cloneStringObject(robj *obj) {
  char *str = zmalloc(sizeof(char) * strlen(obj->ptr));
  strcpy(str, obj->ptr);
  return createStringObject(str, strlen(str));
}

ListNode* ListNodeCreate(void* value) {
  ListNode* listNode = (ListNode *)zmalloc(sizeof(ListNode));
  listNode->value = value;
  listNode->next = NULL;
  return listNode;
}

List* ListCreate() {
  List* list = zmalloc(sizeof(List));
  list->root = NULL;
  list->tail = NULL;
  list->size = 0;
  return list;
}

GraphNode* GraphNodeCreate(robj *key, float value) {
  GraphNode* graphNode = zmalloc(sizeof(GraphNode));
  char *new_name = zmalloc(strlen(key->ptr));
  strcpy(new_name, key->ptr);
  graphNode->key = createStringObject(new_name, strlen(new_name));
  graphNode->key->refcount = 100;
  graphNode->edges = createQuicklistObject();

  quicklistSetOptions(graphNode->edges->ptr, server.list_max_ziplist_size,
      server.list_compress_depth);

  graphNode->edges_hash = dictCreate(&dbDictType, NULL);
  graphNode->incoming = createQuicklistObject();
  graphNode->value = value;
  graphNode->visited = 0;

  // unique Node key
  char *buffer = (char *)(zmalloc(20 * sizeof(char)));
  sprintf(buffer, "M%ld", (unsigned long)(graphNode));
  graphNode->memory_key = createStringObject(buffer, strlen(buffer));
  graphNode->memory_key = tryObjectEncoding(graphNode->memory_key);
  graphNode->memory_key->refcount = 100; //TODO: Fix later

  return graphNode;
}

GraphEdge* GraphEdgeCreate(GraphNode *node1, GraphNode *node2, float value) {
  GraphEdge* graphEdge = zmalloc(sizeof(GraphEdge));
  graphEdge->node1 = node1;
  graphEdge->node2 = node2;
  graphEdge->value = value;

  // unique Edge key
  char *buffer = (char *)(zmalloc(20 * sizeof(char)));
  sprintf(buffer, "M%ld", (unsigned long)(graphEdge));
  graphEdge->memory_key = createStringObject(buffer, strlen(buffer));
  //graphEdge->memory_key = createStringObject("L", 1); // TEMP
  //graphEdge->memory_key = tryObjectEncoding(graphEdge->memory_key);
  graphEdge->memory_key->refcount = 100; //TODO: Fix later

  // Just for testing to make sure it is working
  GraphEdge* u2 = (GraphEdge *)((unsigned long)(atol(buffer)));
  //redisAssert(u2 == graphEdge);

  return graphEdge;
}

void GraphAddNode(Graph *graph, GraphNode *node) {
  ListNode* listNode = ListNodeCreate((void *)(node));
  ListAddNode(graph->nodes, listNode);

  // edges hash
  sds hash_key = (node->key->ptr);
  serverAssert(dictAdd(graph->nodes_hash, hash_key, node) == DICT_OK);
}

void GraphAddEdge(Graph *graph, GraphEdge *graphEdge) {
  ListNode* listNode = ListNodeCreate((void *)(graphEdge));
  ListAddNode(graph->edges, listNode); // TODO: Change !

  // Node 1 edges
  // edges list
  listTypePush(graphEdge->node1->edges, graphEdge->memory_key, LIST_TAIL);
  // edges hash
  sds hash_key = (graphEdge->node2->key->ptr);
  dictAdd(graphEdge->node1->edges_hash, hash_key, graphEdge);

  // Node 2 edges
  if (graph->directed ) {
    listTypePush(graphEdge->node2->incoming, graphEdge->memory_key, LIST_TAIL);
  }
  else { // Undirected
    listTypePush(graphEdge->node2->edges, graphEdge->memory_key, LIST_TAIL);
    sds hash_key2 = (graphEdge->node1->key->ptr);
    dictAdd(graphEdge->node2->edges_hash, hash_key2, graphEdge); // == DICT_OK;
  }
}

void GraphDeleteEdge(Graph *graph, GraphEdge *graphEdge) {
  listTypeEntry entry;
  listTypeIterator *li;

  GraphNode *node1 = graphEdge->node1;
  GraphNode *node2 = graphEdge->node2;

  li = listTypeInitIterator(node1->edges, 0, LIST_TAIL);

  // Deleting from node1
  while (listTypeNext(li,&entry)) {
    if (listTypeEqual(&entry, graphEdge->memory_key)) {
      listTypeDelete(li, &entry);
      break;
    }
  }
  listTypeReleaseIterator(li);
  // Deleting from node1 hash
  dictDeleteNoFree(node1->edges_hash, graphEdge->node2->key->ptr);

  // Deleting from node2
  li = listTypeInitIterator(graph->directed ? node2->incoming : node2->edges, 0, LIST_TAIL);
  while (listTypeNext(li,&entry)) {
    if (listTypeEqual(&entry, graphEdge->memory_key)) {
      listTypeDelete(li, &entry);
      break;
    }
  }
  listTypeReleaseIterator(li);
  if (!graph->directed) {
    // Deleting from node2 hash
    dictDeleteNoFree(node2->edges_hash, graphEdge->node1->key->ptr);
  }

  // Deleting from Graph
  ListDeleteNode(graph->edges, graphEdge);
}

Graph* GraphCreate() {
  Graph* graph = zmalloc(sizeof(Graph));
  graph->nodes = ListCreate();
  graph->edges = ListCreate();
  graph->nodes_hash = dictCreate(&dbDictType, NULL);
  graph->directed = 0;
  return graph;
}

GraphNode* GraphGetNode(Graph *graph, robj *key) {
  dictEntry *entry = dictFind(graph->nodes_hash, key->ptr);
  //serverLog(LL_WARNING,"N %X", entry);

  if (entry == NULL) return NULL;
  GraphNode *node = (GraphNode *)(dictGetVal(entry));

  //serverLog(LL_WARNING,"%X", node);

  return node;
}

GraphNode* GraphGetOrAddNode(Graph *graph, robj *key) {
  dictEntry *entry = dictFind(graph->nodes_hash, key->ptr);

  //serverLog(LL_WARNING,"%X", entry);

  GraphNode *node;
  if (entry == NULL) {
    node = GraphNodeCreate(key, 0);
    GraphAddNode(graph, node);
  } else {
    node = (GraphNode *)(dictGetVal(entry));
  }
  return node;
}

int GraphNodeExists(Graph *graph, robj *key) {
  GraphNode *node = GraphGetNode(graph, key);
  return (node != NULL);
}

GraphEdge* GraphGetEdge(Graph *graph, GraphNode *node1, GraphNode *node2) {
  dictEntry *entry = dictFind(node1->edges_hash, node2->key->ptr);
  if (entry == NULL) return NULL;
  GraphEdge *edge = (GraphEdge *)(dictGetVal(entry));
  return edge;
}

GraphEdge *GraphGetEdgeByKey(Graph *graph, robj *key) {
  unsigned long int_value = atol(key->ptr + sizeof(char));
  GraphEdge *edge1 = (GraphEdge *)(int_value);
  return edge1;
}

void ListAddNode(List *list, ListNode *node) {
  if (list->root == NULL) {
    list->root = node;
    list->tail = node;
  } else {
    list->tail->next = node;
    list->tail = node;
  }
  list->size++;
}

// TODO: Fix for the tail
void ListDeleteNode(List *list, void *value) {
  ListNode* previous = NULL;
  ListNode* current = list->root;

  while (current != NULL) {
    if ((void *)(current->value) == value) {
      if (previous) {
        previous->next = current->next;
      } else {
        list->root = current->next;
      }
      zfree(current);
      list->size--;

      return;
    }

    previous = current;
    current = current->next;
  }

  return;
}

void dijkstra(client *c, Graph *graph, GraphNode *node1, GraphNode *node2) {

  robj *distances_obj = createZsetObject();
  zset *distances = distances_obj->ptr;

  //addReply(c, shared.czero);
  //return C_OK;

  // Initialization
  zslInsert(distances->zsl, 0, (node1->key->ptr));
  dictAdd(distances->dict, (node1->key->ptr), NULL);

  // Main loop
  GraphNode *current_node = node1;
  float current_node_distance;

  current_node_distance = 0;
  int finished = 0;
  float final_distance;

  while (current_node != NULL) {

    // Reached Destination, and print the total distance
    if (equalStringObjects(current_node->key, node2->key)) {
      final_distance = current_node_distance;
      finished = 1;
      break;
    }

    // Deleting the top of the distances
    zskiplistNode *tmp_node;

    zslDelete(distances->zsl, current_node_distance, current_node->key->ptr, &tmp_node);
    dictDelete(distances->dict, current_node->key);

    // Marking the node as visited
    current_node->visited = 1;

    int neighbours_count = listTypeLength(current_node->edges);
    int j;

    GraphEdge *edge;

    for (j = 0; j < neighbours_count; j++) {
      quicklistEntry entry;

      quicklistIndex(current_node->edges->ptr, j, &entry);

      robj *value;
      value = createStringObject((char*)entry.value,entry.sz);
      edge = GraphGetEdgeByKey(graph, value);
      decrRefCount(value);

      GraphNode *neighbour = NULL;

      if (edge->node1 == current_node) {
        neighbour = edge->node2;
      } else if (edge->node2 == current_node) {
        neighbour = edge->node1;
      }

      if (neighbour != NULL) {

        // If neighbour already visited, skip
        // SLOW #TODO, fix
        //if (zzlFind(visited->ptr, neighbour->key, NULL)) {
        //  continue;
        //}
        if (neighbour->visited) continue;

        float distance = edge->value + current_node_distance;
        float neighbour_distance;

        dictEntry *de;
        de = dictFind(distances->dict, neighbour->key);

        if (de != NULL) {
          neighbour_distance = *((float *)dictGetVal(de));
          if (distance < neighbour_distance) {
            // Deleting
            zskiplistNode *tmp_node;
            zslDelete(distances->zsl, neighbour_distance, neighbour->key->ptr, &tmp_node);
            // Inserting again
            zslInsert(distances->zsl, distance, (neighbour->key->ptr));
            // Update the parent
            neighbour->parent = current_node;

          }
        } else {
          zslInsert(distances->zsl, distance, (neighbour->key->ptr));
          float *float_loc = zmalloc(sizeof(float));
          *float_loc = distance;
          dictAdd(distances->dict, neighbour->key->ptr, float_loc);
          zfree(float_loc);
          neighbour->parent = current_node;
        }

      }
    }

    // READING MINIMUM
    // FOOTER
    zskiplistNode *first_node = distances->zsl->header->level[0].forward;

    robj *key;
    if (!finished && first_node != NULL) {
      key = createStringObject(first_node->ele, sdslen(first_node->ele));
      current_node = GraphGetNode(graph, key);
      decrRefCount(key);
      current_node_distance = first_node->score;
    } else  {
      current_node = NULL;
    }
  }

  // Building reply
  GraphNode *cur_node = node2;
  int count = 1;

  while(cur_node != NULL) {
    cur_node = cur_node->parent;
    if (cur_node != NULL)
      count++;
    if (equalStringObjects(cur_node->key, node1->key)) {
      break;
    }
  }

  addReplyMultiBulkLen(c, count + 1);
  cur_node = node2;

  robj **replies = zmalloc(sizeof(robj *) * count);
  int k = count - 1;
  replies[k--] = node2->key;

  while(cur_node != NULL) {
    cur_node = cur_node->parent;
    if (cur_node != NULL) {
      replies[k--] = cur_node->key;
    }
    if (equalStringObjects(cur_node->key, node1->key)) {
      break;
    }
  }
  //redisAssert(k == -1);

  // Path nodes reversed
  for(k = 0; k < count; k++)
    addReplyBulk(c, replies[k]);

  robj *distance_reply = createStringObjectFromLongDouble(final_distance, 0);
  addReplyBulk(c, distance_reply);
  decrRefCount(distance_reply);

  // Clear memory
  zfree(replies);
  decrRefCount(distances);

  return C_OK;
}

void gshortestpathCommand(client *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);

  GraphNode *node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *node2 = GraphGetNode(graph_object, c->argv[3]);

  ListNode *current_node;
  current_node = graph_object->nodes->root;
  while (current_node != NULL) {
    GraphNode *graphNode = (GraphNode *)(current_node->value);
    graphNode->parent = NULL;
    graphNode->visited = 0;
    current_node = current_node->next;
  }

  serverAssert(node1 != NULL);
  serverAssert(node2 != NULL);

  dijkstra(c, graph_object, node1, node2);
}

robj *createGraphObject() {
  Graph *ptr = GraphCreate();
  robj *obj = createObject(OBJ_GRAPH, ptr);
  obj->refcount = 100;
  obj->encoding = OBJ_GRAPH;
  return obj;
}

void gmintreeCommand(client *c) {
  // Using Prim's Algorithm
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);

  robj *graph2_key = c->argv[2];

  robj *graph2 = createGraphObject();
  Graph *graph2_object = (Graph *)(graph2->ptr);
  dbAdd(c->db, graph2_key, graph2);

  // Adding first node
  GraphNode *node, *root;
  root = (GraphNode *)(graph_object->nodes->root->value);
  node = GraphNodeCreate(root->key, 0);
  GraphAddNode(graph2_object, node);

  // Creating a priority-queue for edges
  robj *queue = createZsetObject();
  zset *qzs = queue->ptr;

  // TODO: Make sure first node has edges, and that's not a problem, but it should be a connected graph !

  // Insert the first node edges to the queue
  robj *list = root->edges;
  robj *edge_key;
  GraphEdge *edge;
  int count, i;
  count = listTypeLength(list);
  for(i = 0; i < count; i++) {
    edge_key = listNodeValue(listIndex(list->ptr, i));
    edge = GraphGetEdgeByKey(graph_object, edge_key);
    zslInsert(qzs->zsl, edge->value, edge->memory_key);
  }

  // While the minimum edge connects existing node to new node, or BETTER: until the
  // new graph nodes length == graph 1 nodes length
  while (1) {
    zskiplistNode *node;
    node = qzs->zsl->header->level[0].forward;
    if (node != NULL) {
      GraphEdge *edge = GraphGetEdgeByKey(graph_object, node->ele);
      zslDelete(qzs->zsl, node->score, edge->memory_key, &node); // MODIFIED
      GraphNode *node1, *node2;
      node1 = edge->node1;
      node2 = edge->node2;

      char a, b;
      a = GraphNodeExists(graph2_object, node1->key);
      b = GraphNodeExists(graph2_object, node2->key);

      if (a ^ b) {

        // HERE: Add the new node to graph2
        GraphNode *node_to_add = a ? GraphNodeCreate(node2->key, 0) : GraphNodeCreate(node1->key, 0);
        GraphAddNode(graph2_object, node_to_add);

        // Adding the edge to graph2
        GraphNode *temp = GraphGetNode(graph2_object, a ? node1->key: node2->key);
        GraphEdge *new_edge = GraphEdgeCreate(temp, node_to_add, edge->value);
        GraphAddEdge(graph2_object, new_edge);

        // Adding the node edges to the queue, if they don't exist before
        list = (a ? node2 : node1)->edges;
        count = listTypeLength(list);
        for(i = 0; i < count; i++) {
          GraphEdge *edge2;
          edge_key = listNodeValue(listIndex(list->ptr, i));
          edge2 = GraphGetEdgeByKey(graph_object, edge_key);

          GraphNode *g2_node1 = GraphGetNode(graph2_object, edge2->node1->key);
          GraphNode *g2_node2 = GraphGetNode(graph2_object, edge2->node2->key);
          // Check if the edge already exists in graph2
          //GraphNode *tmp_edge = GraphGetEdgeByKey(graph2_object, edge_key);
          if ((g2_node1 != NULL) && (g2_node2 != NULL)) {
            // Nothing
          } else {
            zslInsert(qzs->zsl, edge2->value, edge2->memory_key);
          }
        }

      }

    } else {
      break;
    }
  }

  RETURN_OK
    return;

}

void gsetdirectedCommand(client *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyWrite(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  graph_object->directed = 1;

  RETURN_OK
}

void gvertexCommand(client *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyWrite(c->db, key);
  robj *value;
  if (graph == NULL) {
    graph = createGraphObject();
    dbAdd(c->db, key, graph);
  }

  Graph *graph_object = (Graph *)(graph->ptr);

  int added = 0;

  int i;
  for (i = 2; i < (c->argc); i++) {
    GraphNode *graph_node = GraphGetNode(graph_object, c->argv[i]);
    //serverLog(LL_WARNING, "%X", graph_object);
    //serverLog(LL_WARNING,"%d %s\n", graph_node, c->argv[i]->ptr);
    if (graph_node == NULL) {
      //serverLog(LL_WARNING, "ADDINg");
      graph_node = GraphNodeCreate(c->argv[i], 0);
      GraphAddNode(graph_object, graph_node);
      added++;
    }
  }

  addReplyLongLong(c, added);
  return C_OK;
}

void gincomingCommand(client *c) {
  robj *graph;
  robj *edge_key;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *node = GraphGetNode(graph_object, c->argv[2]);

  // Neighbours count
  long count = listTypeLength(node->incoming);
  addReplyMultiBulkLen(c, count);
  int i;
  robj *list = node->incoming;
  for (i = 0; i < count; i++) {
    edge_key = listNodeValue(listIndex(list->ptr, i));
    edge = GraphGetEdgeByKey(graph_object, edge_key);
    if (equalStringObjects(edge->node1->key, node->key)) {
      addReplyBulk(c, edge->node2->key);
    } else {
      addReplyBulk(c, edge->node1->key);
    }
  }

  return C_OK;
}

void gneighboursCommand(client *c) {
  robj *graph;
  robj *edge_key;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *node = GraphGetNode(graph_object, c->argv[2]);

  // Neighbours count
  int i;
  robj *list = node->edges;
  long count = listTypeLength(list);
  addReplyMultiBulkLen(c, count);

  quicklistEntry entry;

  for (i = 0; i < count; i++) {
    quicklistIndex(list->ptr, i, &entry);
    robj *value;
    value = createStringObject((char*)entry.value,entry.sz);
    edge = GraphGetEdgeByKey(graph_object, value);
    //decrRefCount(value);
    if (equalStringObjects(edge->node1->key, node->key)) {
      addReplyBulk(c, edge->node2->key);
    } else {
      addReplyBulk(c, edge->node1->key);
    }
  }

  return C_OK;
}

robj *neighboursToSet(GraphNode *node, Graph *graph_object) {

  // Creating neighbours set of the first node

  GraphEdge *edge;
  robj *set;
  robj *edge_key;

  set = setTypeCreate(node->key->ptr);

  long count;

  count = listTypeLength(node->edges);
  robj *list = node->edges;
  int i;

  quicklistEntry entry;

  for (i = 0; i < count; i++) {
    quicklistIndex(list->ptr, i, &entry);
    edge_key = createStringObject((char*)entry.value,entry.sz);
    edge = GraphGetEdgeByKey(graph_object, edge_key);
    robj *neighbour_key;
    if (equalStringObjects(edge->node1->key, node->key)) {
      neighbour_key = edge->node2->key;
    } else {
      neighbour_key = edge->node1->key;
    }
    if (set == NULL) {
      //set = setTypeCreate(neighbour_key);
      //setTypeAdd(set, neighbour_key);
    } else {
    }
    setTypeAdd(set, neighbour_key->ptr);
    // TODO :Free up edge_key;
  }

  return set;
}

void gcommonCommand(client *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *node2 = GraphGetNode(graph_object, c->argv[3]);

  if (node1 == NULL || node2 == NULL) {
    addReplyMultiBulkLen(c, 0);
    return C_OK;
  }

  robj *set1 = NULL;
  robj *set2 = NULL;

  set1 = neighboursToSet(node1, graph_object);
  set2 = neighboursToSet(node2, graph_object);

  if (set1 == NULL || set2 == NULL) {
    addReplyMultiBulkLen(c, 0);
    return C_OK;
  }

  //robj *result = createIntsetObject();
  robj *result = createSetObject();

  // Switch set1, and set2 if set1 length is bigger. To improve performance
  robj *temp;
  //if (setTypeSize(set1) > setTypeSize(set2)) {
  //  temp = set2;
  //  set2 = set1;
  //  set1 = temp;
  //}

  setTypeIterator *si = setTypeInitIterator(set1);
  int encoding;
  int intobj;
  sds *eleobj;

  while(eleobj = setTypeNextObject(si)) {
    //setTypeAdd(result, createStringObject("QUNSUL", 6)->ptr);
    char *l = eleobj; // FOR DEBUGGING
    if (setTypeIsMember(set2, eleobj)){
      setTypeAdd(result, eleobj);
    }
  }
  setTypeReleaseIterator(si);

  addReplyMultiBulkLen(c, setTypeSize(result));

  si = setTypeInitIterator(result);
  //addReplyBulk(c, createStringObject("OMAR", 4));
  while(eleobj = setTypeNextObject(si)) {
    addReplyBulk(c, createStringObject(eleobj, sdslen(eleobj)));
  }
  setTypeReleaseIterator(si);

  freeSetObject(set1);
  freeSetObject(set2);
  freeSetObject(result);

  return C_OK;
}

void gvertexexistsCommand(client *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *graph_node = GraphGetNode(graph_object, c->argv[2]);
  if (graph_node != NULL) {
    addReply(c, shared.cone);
  } else {
    addReply(c, shared.czero);
  }
  return C_OK;
}

void gedgeexistsCommand(client *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *graph_node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *graph_node2 = GraphGetNode(graph_object, c->argv[3]);

  // Return zero if any of the nodes is/are null
  if ((graph_node1 == NULL) || (graph_node2 == NULL)) {
    addReply(c, shared.czero);
    return C_OK;
  }

  // Check whether the edge already exists
  edge = NULL;
  if (graph_node1 != NULL && graph_node2 != NULL)
    edge = GraphGetEdge(graph_object, graph_node1, graph_node2);

  if (edge != NULL) {
    addReply(c, shared.cone);
  } else {
    addReply(c, shared.czero);
  }
  return C_OK;
}

void gedgevalueCommand(client *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *graph_node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *graph_node2 = GraphGetNode(graph_object, c->argv[3]);

  // Return zero if any of the nodes is/are null
  if ((graph_node1 == NULL) || (graph_node2 == NULL)) {
    addReply(c, shared.czero);
    return C_OK;
  }

  // Check whether the edge already exists
  edge = NULL;
  if (graph_node1 != NULL && graph_node2 != NULL)
    edge = GraphGetEdge(graph_object, graph_node1, graph_node2);

  if (edge != NULL) {
    addReplyLongLong(c, edge->value);
  } else {
    addReply(c, shared.czero);
  }
  return C_OK;
}

void gedgeCommand(client *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);

  Graph *graph_object = (Graph *)(graph->ptr);

  if (equalStringObjects(c->argv[2], c->argv[3])) {
    addReply(c, shared.czero);
    return C_OK;
  }

  GraphNode *graph_node1 = GraphGetOrAddNode(graph_object, c->argv[2]);
  GraphNode *graph_node2 = GraphGetOrAddNode(graph_object, c->argv[3]);

  // Check whether the edge already exists
  edge = GraphGetEdge(graph_object, graph_node1, graph_node2);

  char *value_string = c->argv[4]->ptr;
  float value_float = atof(value_string);

  if (edge != NULL) {
    edge->value = value_float;
    addReply(c, shared.czero);
    return C_OK;
  } else {
    edge = GraphEdgeCreate(graph_node1, graph_node2, value_float);
    GraphAddEdge(graph_object, edge);

    robj *value;
    addReply(c, shared.cone);
    return C_OK;
  }
}

void gedgeremCommand(client *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);

  Graph *graph_object = (Graph *)(graph->ptr);

  GraphNode *graph_node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *graph_node2 = GraphGetNode(graph_object, c->argv[3]);

  // Check whether the edge already exists
  edge = GraphGetEdge(graph_object, graph_node1, graph_node2);

  if (edge) {
    GraphDeleteEdge(graph_object, edge);
    addReply(c, shared.cone);
  } else {
    addReply(c, shared.czero);
  }

  return C_OK;
}

void gedgeincrbyCommand(client *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *graph_node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *graph_node2 = GraphGetNode(graph_object, c->argv[3]);

  char *value_string = c->argv[4]->ptr;
  float value_float = atof(value_string);

  // Check whether the edge already exists
  edge = GraphGetEdge(graph_object, graph_node1, graph_node2);

  if (edge != NULL) {
    edge->value += value_float;
    addReplyLongLong(c, edge->value);
    return C_OK;
  } else {
    gedgeCommand(c);
  }
}

void gverticesCommand(client *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);

  if (graph == NULL || checkType(c, graph, OBJ_GRAPH)) {
    addReply(c, shared.czero);
    return;
  }

  Graph *graph_object = (Graph *)(graph->ptr);

  List *graphNodes = graph_object->nodes;
  ListNode *current_node;

  int count = graphNodes->size;
  addReplyMultiBulkLen(c, count);

  current_node = graphNodes->root;
  while (current_node != NULL) {
    GraphNode *graphNode = (GraphNode *)(current_node->value);
    addReplyBulkSds(c, graphNode->key->ptr);
    current_node = current_node->next;
  }

  return C_OK;
}

void gedgesCommand(client *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);

  Graph *graph_object = (Graph *)(graph->ptr);

  List *graphEdges = graph_object->edges;
  ListNode *current_node = graphEdges->root;

  int count = 0;
  while (current_node != NULL) {
    count++;
    current_node = current_node->next;
  }

  addReplyMultiBulkLen(c, count * 3);

  robj *reply = createStringObject("Done", strlen("Done"));

  current_node = graphEdges->root;
  while (current_node != NULL) {
    GraphEdge *graphEdge = (GraphNode *)(current_node->value);
    robj *reply1 = graphEdge->node1->key;
    addReplyBulk(c, reply1);
    robj *reply2 = graphEdge->node2->key;
    addReplyBulk(c, reply2);
    robj *reply3 = createStringObjectFromLongDouble(graphEdge->value, 0);
    addReplyBulk(c, reply3);
    current_node = current_node->next;
  }

  return C_OK;
}

void testCommand(client *c) {
  // Writing and reading from a hash
  /*
  dict *d = dictCreate(&dbDictType, NULL);
  robj *key = createStringObject("key", strlen("key"));
  sds key_str = (key->ptr);
  robj *value = createStringObject("omar", strlen("omar"));
  dictAdd(d, key_str, value);
  dictEntry *entry = dictFind(d,key->ptr);
  robj *get_value = (robj *)(dictGetVal(entry));
  */

  robj *key = createStringObject("key", strlen("key"));
  robj *set;
  set = setTypeCreate(key);
  //setTypeAdd(set,c->argv[j]->ptr)
  
  RETURN_OK
}

int delayedPublish(struct aeEventLoop *eventLoop, long long id, void *clientData) {
  robj **arr = clientData;
  robj *str1 = arr[0];
  robj *str2 = arr[1];

  int receivers = pubsubPublishMessage(str1, str2);
  //forceCommandPropagation(c, REDIS_PROPAGATE_REPL);
  aeDeleteTimeEvent(eventLoop, id);
  freeStringObject(str1);
  freeStringObject(str2);
}


void dpublishCommand(client *c) {
    robj *str1 = dupStringObject(c->argv[1]);
    robj *str2 = dupStringObject(c->argv[2]);
    robj **arr = zmalloc(sizeof(robj *) * 2);
    arr[0] = str1;
    arr[1] = str2;
    if(aeCreateTimeEvent(server.el, 20000, delayedPublish, arr, NULL) == AE_ERR) {

    }
  addReplyLongLong(c, 0);
}

