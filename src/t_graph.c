#include "server.h"
#include "t_graph.h"
#include <math.h> /* isnan(), isinf() */

#define RETURN_OK \
  robj* value10 = createStringObject("OK", strlen("OK")); \
  addReplyBulk(c, value10); \
  decrRefCount(value10);

#define RETURN_REPLY \
  addReplyBulk(c, reply); \
  return REDIS_OK;

#define RETURN_CANCEL \
  robj* value20 = createStringObject("Cancel", strlen("Cancel")); \
  addReplyBulk(c, value20); \
  return REDIS_OK;

void freeGraphObject(robj *graph_object) {
  //TODO
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

GraphNode* GraphNodeCreate(sds key, float value) {
  GraphNode* graphNode = zmalloc(sizeof(GraphNode));
  graphNode->key = sdsdup(key);
  graphNode->edges = createQuicklistObject();

  quicklistSetOptions(graphNode->edges->ptr, server.list_max_ziplist_size,
      server.list_compress_depth);

  graphNode->edges_hash = dictCreate(&dbDictType, NULL);
  graphNode->incoming = createQuicklistObject();
  graphNode->value = value;
  graphNode->visited = 0;

  // unique Node key
  graphNode->memory_key = sdsfromlonglong((unsigned long)(graphNode));

  return graphNode;
}

GraphEdge* GraphEdgeCreate(GraphNode *node1, GraphNode *node2, float value) {
  GraphEdge* graphEdge = zmalloc(sizeof(GraphEdge));
  graphEdge->node1 = node1;
  graphEdge->node2 = node2;
  graphEdge->value = value;

  // unique Edge key
  graphEdge->memory_key = sdsfromlonglong((unsigned long)(graphEdge));

  // Just for testing to make sure it is working
  GraphEdge* u2 = (GraphEdge *)((unsigned long)(atol(graphEdge->memory_key)));
  serverAssert(u2 == graphEdge);

  return graphEdge;
}

void GraphAddNode(Graph *graph, GraphNode *node) {
  ListNode* listNode = ListNodeCreate((void *)(node));
  ListAddNode(graph->nodes, listNode);

  // edges hash
  serverAssert(dictAdd(graph->nodes_hash, node->key, node) == DICT_OK);
}

void GraphAddEdge(Graph *graph, GraphEdge *graphEdge) {
  ListNode* listNode = ListNodeCreate((void *)(graphEdge));
  ListAddNode(graph->edges, listNode);

  // Node 1 edges
  // edges list
  listTypePush(graphEdge->node1->edges, createStringObject(graphEdge->memory_key, sdslen(graphEdge->memory_key)), LIST_TAIL);
  // edges hash
  dictAdd(graphEdge->node1->edges_hash, graphEdge->node2->key, graphEdge);

  // Node 2 edges
  if (graph->directed) {
    listTypePush(graphEdge->node2->incoming, createStringObject(graphEdge->memory_key, sdslen(graphEdge->memory_key)), LIST_TAIL);
  }
  else { // Undirected
    listTypePush(graphEdge->node2->edges, createStringObject(graphEdge->memory_key, sdslen(graphEdge->memory_key)), LIST_TAIL);
    dictAdd(graphEdge->node2->edges_hash, graphEdge->node1->key, graphEdge); // == DICT_OK;
  }
}

void GraphDeleteEdge(Graph *graph, GraphEdge *graphEdge) {
  listTypeEntry entry;
  listTypeIterator *li;

  GraphNode *node1 = graphEdge->node1;
  GraphNode *node2 = graphEdge->node2;

  li = listTypeInitIterator(node1->edges, 0, LIST_TAIL);

  // Deleting from node1
  _Bool equal;
  robj * key;
  while (listTypeNext(li,&entry)) {
    key = createStringObject(graphEdge->memory_key, sdslen(graphEdge->memory_key));
    equal = listTypeEqual(&entry, key);
    decrRefCount(key);
    if (equal) {
      listTypeDelete(li, &entry);
      break;
    }
  }
  listTypeReleaseIterator(li);
  // Deleting from node1 hash
  dictDeleteNoFree(node1->edges_hash, graphEdge->node2->key);

  // Deleting from node2
  li = listTypeInitIterator(graph->directed ? node2->incoming : node2->edges, 0, LIST_TAIL);
  while (listTypeNext(li,&entry)) {
    key = createStringObject(graphEdge->memory_key, sdslen(graphEdge->memory_key));
    equal = listTypeEqual(&entry, key);
    decrRefCount(key);
    if (equal) {
      listTypeDelete(li, &entry);
      break;
    }
  }
  listTypeReleaseIterator(li);
  if (!graph->directed) {
    // Deleting from node2 hash
    dictDeleteNoFree(node2->edges_hash, graphEdge->node1->key);
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

GraphNode* GraphGetNode(Graph *graph, sds key) {
  dictEntry *entry = dictFind(graph->nodes_hash, key);

  if (entry == NULL) return NULL;
  GraphNode *node = (GraphNode *)(dictGetVal(entry));

  return node;
}

GraphNode* GraphGetOrAddNode(Graph *graph, sds key) {
  dictEntry *entry = dictFind(graph->nodes_hash, key);

  GraphNode *node;
  if (entry == NULL) {
    node = GraphNodeCreate(key, 0);
    GraphAddNode(graph, node);
  } else {
    node = (GraphNode *)(dictGetVal(entry));
  }
  return node;
}

int GraphNodeExists(Graph *graph, sds key) {
  GraphNode *node = GraphGetNode(graph, key);
  return (node != NULL);
}

GraphEdge* GraphGetEdge(Graph *graph, GraphNode *node1, GraphNode *node2) {
  dictEntry *entry = dictFind(node1->edges_hash, node2->key);
  if (entry == NULL) return NULL;
  GraphEdge *edge = (GraphEdge *)(dictGetVal(entry));
  return edge;
}

GraphEdge *GraphGetEdgeByKey(Graph *graph, sds key) {
  GraphEdge *edge = (GraphEdge *)((unsigned long)(atol(key)));
  return edge;
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

  // Initialization
  zslInsert(distances->zsl, 0, sdsdup(node1->key));
  dictAdd(distances->dict, node1->key, NULL);

  // Main loop
  GraphNode *current_node = node1;
  float current_node_distance;

  current_node_distance = 0;
  int finished = 0;
  float final_distance;
  zskiplistNode *tmp_node = NULL;

  while (current_node != NULL) {

    // Reached Destination, and print the total distance
    if (sdscmp(current_node->key, node2->key) == 0) {
      final_distance = current_node_distance;
      finished = 1;
      break;
    }

    // Deleting the top of the distances
    zslDelete(distances->zsl, current_node_distance, current_node->key, &tmp_node);
    zfree(tmp_node);
    dictDelete(distances->dict, current_node->key);

    // Marking the node as visited
    current_node->visited = 1;

    int neighbours_count = listTypeLength(current_node->edges);
    int j;

    GraphEdge *edge;

    for (j = 0; j < neighbours_count; j++) {
      quicklistEntry entry;

      quicklistIndex(current_node->edges->ptr, j, &entry);

      sds key = sdsfromlonglong(entry.longval);
      edge = GraphGetEdgeByKey(graph, key);
      sdsfree(key);

      GraphNode *neighbour = NULL;

      if (edge->node1 == current_node) {
        neighbour = edge->node2;
      } else if (edge->node2 == current_node) {
        neighbour = edge->node1;
      }

      if (neighbour != NULL) {

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
            zslDelete(distances->zsl, neighbour_distance, neighbour->key, &tmp_node);
            //zfree(tmp_node);
            // Inserting again
            zslInsert(distances->zsl, distance, sdsdup(neighbour->key));
            // Update the parent
            neighbour->parent = current_node;

          }
        } else {
          zslInsert(distances->zsl, distance, sdsdup(neighbour->key));
          float *float_loc = zmalloc(sizeof(float));
          *float_loc = distance;
          dictAdd(distances->dict, neighbour->key, float_loc);
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
      current_node = GraphGetNode(graph, first_node->ele);
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
    if (sdscmp(cur_node->key, node1->key) == 0) {
      break;
    }
  }

  addReplyMultiBulkLen(c, count + 1);
  cur_node = node2;

  robj **replies = zmalloc(sizeof(robj *) * count);
  int k = count - 1;
  replies[k--] = createStringObject(node2->key, sdslen(node2->key));

  while(cur_node != NULL) {
    cur_node = cur_node->parent;
    if (cur_node != NULL) {
      replies[k--] = createStringObject(cur_node->key, sdslen(cur_node->key));
    }
    if (sdscmp(cur_node->key, node1->key) == 0) {
      break;
    }
  }
  serverAssert(k == -1);

  // Path nodes reversed
  for(k = 0; k < count; k++)
    addReplyBulk(c, replies[k]);

  robj *distance_reply = createStringObjectFromLongDouble(final_distance, 0);
  addReplyBulk(c, distance_reply);
  decrRefCount(distance_reply);

  // Clear memory
  zfree(replies);
  decrRefCount(distances_obj);

  return;
}

void gshortestpathCommand(client *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);

  GraphNode *node1 = GraphGetNode(graph_object, c->argv[2]->ptr);
  GraphNode *node2 = GraphGetNode(graph_object, c->argv[3]->ptr);

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
  long count = listTypeLength(list);
  quicklistEntry entry;

  int i;
  GraphEdge *edge;
  for(i = 0; i < count; i++) {
    quicklistIndex(list->ptr, i, &entry);
    sds value;
    value = sdsfromlonglong(entry.longval);
    edge = GraphGetEdgeByKey(graph_object, value);
    sdsfree(value);
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
      zfree(node);
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
          quicklistIndex(list->ptr, i, &entry);
          sds value = sdsfromlonglong(entry.longval);
          edge2 = GraphGetEdgeByKey(graph_object, value);
          //zfree(entry);

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
    sds key = sdsnew(c->argv[i]->ptr);
    GraphNode *graph_node = GraphGetNode(graph_object, key);
    if (graph_node == NULL) {
      graph_node = GraphNodeCreate(key, 0);
      GraphAddNode(graph_object, graph_node);
      added++;
    } else {
      sdsfree(key);
    }
  }

  addReplyLongLong(c, added);
  return;
}

void gincomingCommand(client *c) {
  robj *graph;
  robj *edge_key;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *node = GraphGetNode(graph_object, c->argv[2]->ptr);

  // Neighbours count
  int i;
  robj *list = node->incoming;
  long count = listTypeLength(list);
  addReplyMultiBulkLen(c, count);

  quicklistEntry entry;

  for (i = 0; i < count; i++) {
    quicklistIndex(list->ptr, i, &entry);
    robj *value;
    value = sdsfromlonglong(entry.longval);
    edge = GraphGetEdgeByKey(graph_object, value);
    sdsfree(value);
    serverAssert(edge != NULL);
    if (sdscmp(edge->node1->key, node->key) == 0) {
      addReplyBulk(c, createStringObject(sdsdup(edge->node2->key), sdslen(edge->node2->key)));
    } else {
      addReplyBulk(c, createStringObject(sdsdup(edge->node1->key), sdslen(edge->node1->key)));
    }
  }

  return;
}

void gneighboursCommand(client *c) {
  robj *graph;
  robj *edge_key;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *node = GraphGetNode(graph_object, c->argv[2]->ptr);

  // Neighbours count
  int i;
  robj *list = node->edges;
  long count = listTypeLength(list);
  addReplyMultiBulkLen(c, count);

  quicklistEntry entry;

  for (i = 0; i < count; i++) {
    quicklistIndex(list->ptr, i, &entry);
    sds value;
    value = sdsfromlonglong(entry.longval);
    edge = GraphGetEdgeByKey(graph_object, value);
    sdsfree(value);
    serverAssert(edge != NULL);
    if (sdscmp(edge->node1->key, node->key) == 0) {
      addReplyBulk(c, createStringObject(sdsdup(edge->node2->key), sdslen(edge->node2->key)));
    } else {
      addReplyBulk(c, createStringObject(sdsdup(edge->node1->key), sdslen(edge->node1->key)));
    }
  }

  return;
}

robj *neighboursToSet(GraphNode *node, Graph *graph_object) {

  // Creating neighbours set of the first node

  GraphEdge *edge;
  robj *set;
  sds edge_key;

  set = setTypeCreate(node->key);

  long count;

  count = listTypeLength(node->edges);
  robj *list = node->edges;
  int i;

  quicklistEntry entry;

  for (i = 0; i < count; i++) {
    quicklistIndex(list->ptr, i, &entry);
    edge_key = sdsfromlonglong(entry.longval);
    edge = GraphGetEdgeByKey(graph_object, edge_key);
    sdsfree(edge_key);
    sds neighbour_key;
    if (sdscmp(edge->node1->key, node->key) == 0) {
      neighbour_key = sdsdup(edge->node2->key);
    } else {
      neighbour_key = sdsdup(edge->node1->key);
    }
    setTypeAdd(set, neighbour_key);
  }

  return set;
}

void gcommonCommand(client *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *node1 = GraphGetNode(graph_object, c->argv[2]->ptr);
  GraphNode *node2 = GraphGetNode(graph_object, c->argv[3]->ptr);

  if (node1 == NULL || node2 == NULL) {
    addReplyMultiBulkLen(c, 0);
    return;
  }

  robj *set1 = NULL;
  robj *set2 = NULL;

  set1 = neighboursToSet(node1, graph_object);
  set2 = neighboursToSet(node2, graph_object);

  if (set1 == NULL || set2 == NULL) {
    addReplyMultiBulkLen(c, 0);
    return;
  }

  //robj *result = createIntsetObject();
  robj *result = createSetObject();

  // Switch set1, and set2 if set1 length is bigger. To improve performance
  robj *temp;

  setTypeIterator *si = setTypeInitIterator(set1);
  int encoding;
  int intobj;
  sds *eleobj;

  while(eleobj = setTypeNextObject(si)) {
    char *l = eleobj; // FOR DEBUGGING
    if (setTypeIsMember(set2, eleobj)){
      setTypeAdd(result, eleobj);
    }
  }
  setTypeReleaseIterator(si);

  addReplyMultiBulkLen(c, setTypeSize(result));

  si = setTypeInitIterator(result);
  while(eleobj = setTypeNextObject(si)) {
    addReplyBulk(c, createStringObject(eleobj, sdslen(eleobj)));
  }
  setTypeReleaseIterator(si);

  freeSetObject(set1); // decReft
  freeSetObject(set2);
  freeSetObject(result);

  return;
}

void gvertexexistsCommand(client *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *graph_node = GraphGetNode(graph_object, c->argv[2]->ptr);
  if (graph_node != NULL) {
    addReply(c, shared.cone);
  } else {
    addReply(c, shared.czero);
  }
  return;
}

void gedgeexistsCommand(client *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *graph_node1 = GraphGetNode(graph_object, c->argv[2]->ptr);
  GraphNode *graph_node2 = GraphGetNode(graph_object, c->argv[3]->ptr);

  // Return zero if any of the nodes is/are null
  if ((graph_node1 == NULL) || (graph_node2 == NULL)) {
    addReply(c, shared.czero);
    return;
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
  return;
}

void gedgevalueCommand(client *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *graph_node1 = GraphGetNode(graph_object, c->argv[2]->ptr);
  GraphNode *graph_node2 = GraphGetNode(graph_object, c->argv[3]->ptr);

  // Return zero if any of the nodes is/are null
  if ((graph_node1 == NULL) || (graph_node2 == NULL)) {
    addReply(c, shared.czero);
    return;
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
  return;
}

void gedgeCommand(client *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);

  Graph *graph_object = (Graph *)(graph->ptr);

  if (equalStringObjects(c->argv[2], c->argv[3])) {
    addReply(c, shared.czero);
    return;
  }

  GraphNode *graph_node1 = GraphGetOrAddNode(graph_object, sdsnew(c->argv[2]->ptr));
  GraphNode *graph_node2 = GraphGetOrAddNode(graph_object, sdsnew(c->argv[3]->ptr));

  // Check whether the edge already exists
  edge = GraphGetEdge(graph_object, graph_node1, graph_node2);

  char *value_string = c->argv[4]->ptr;
  float value_float = atof(value_string);

  if (edge != NULL) {
    edge->value = value_float;
    addReply(c, shared.czero);
    return;
  } else {
    edge = GraphEdgeCreate(graph_node1, graph_node2, value_float);
    GraphAddEdge(graph_object, edge);

    robj *value;
    addReply(c, shared.cone);
    return;
  }
}

void gedgeremCommand(client *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);

  Graph *graph_object = (Graph *)(graph->ptr);

  GraphNode *graph_node1 = GraphGetNode(graph_object, c->argv[2]->ptr);
  GraphNode *graph_node2 = GraphGetNode(graph_object, c->argv[3]->ptr);

  // Check whether the edge already exists
  edge = GraphGetEdge(graph_object, graph_node1, graph_node2);

  if (edge) {
    GraphDeleteEdge(graph_object, edge);
    addReply(c, shared.cone);
  } else {
    addReply(c, shared.czero);
  }

  return;
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
    return;
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
    addReplyBulkSds(c, graphNode->key);
    current_node = current_node->next;
  }

  return;
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
    robj *reply1 = createStringObject(graphEdge->node1->key, sdslen(graphEdge->node1->key));
    addReplyBulk(c, reply1);
    robj *reply2 = createStringObject(graphEdge->node2->key, sdslen(graphEdge->node2->key));
    addReplyBulk(c, reply2);
    robj *reply3 = createStringObjectFromLongDouble(graphEdge->value, 0);
    addReplyBulk(c, reply3);
    current_node = current_node->next;
  }

  return;
}

void testCommand(client *c) {
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

