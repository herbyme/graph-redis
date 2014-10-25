#include "redis.h"
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
  list->size = 0;
  return list;
}

GraphNode* GraphNodeCreate(robj *key, float value) {
  GraphNode* graphNode = zmalloc(sizeof(GraphNode));
  char *new_name = zmalloc(strlen(key->ptr));
  strcpy(new_name, key->ptr);
  graphNode->key = createStringObject(new_name, strlen(new_name));
  graphNode->key->refcount = 100;
  graphNode->edges = createListObject();
  graphNode->edges_hash = dictCreate(&dbDictType, NULL);
  graphNode->incoming = createListObject();
  graphNode->value = value;
  return graphNode;
}

GraphEdge* GraphEdgeCreate(GraphNode *node1, GraphNode *node2, float value) {
  GraphEdge* graphEdge = zmalloc(sizeof(GraphEdge));
  graphEdge->node1 = node1;
  graphEdge->node2 = node2;
  graphEdge->value = value;

  // unique Edge key
  char *buffer = (char *)(zmalloc(20 * sizeof(char)));
  sprintf(buffer, "%d", (int)(buffer));
  graphEdge->key = createStringObject(buffer, strlen(buffer));
  graphEdge->key->refcount = 100;

  return graphEdge;
}

void GraphAddNode(Graph *graph, GraphNode *node) {
  ListNode* listNode = ListNodeCreate((void *)(node));
  ListAddNode(graph->nodes, listNode);
}

void GraphAddEdge(Graph *graph, GraphEdge *graphEdge) {
  ListNode* listNode = ListNodeCreate((void *)(graphEdge));
  ListAddNode(graph->edges, listNode); // TODO: Change !

  // Node 1 edges
  // edges list
  listTypePush(graphEdge->node1->edges, graphEdge->key, REDIS_TAIL);
  // edges hash
  sds hash_key = sdsdup(graphEdge->node2->key->ptr);
  redisAssert(dictAdd(graphEdge->node1->edges_hash, hash_key, graphEdge) == DICT_OK);

  // Node 2 edges
  if (graph->directed ) {
    listTypePush(graphEdge->node2->incoming, graphEdge->key, REDIS_TAIL);
  }
  else { // Undirected
    listTypePush(graphEdge->node2->edges, graphEdge->key, REDIS_TAIL);
    sds hash_key2 = sdsdup(graphEdge->node1->key->ptr);
    redisAssert(dictAdd(graphEdge->node2->edges_hash, hash_key2, graphEdge) == DICT_OK);
  }
}

void GraphDeleteEdge(Graph *graph, GraphEdge *graphEdge) {
  listTypeEntry entry;
  listTypeIterator *li;

  GraphNode *node1 = graphEdge->node1;
  GraphNode *node2 = graphEdge->node2;

  li = listTypeInitIterator(node1->edges, 0, REDIS_TAIL);

  // Deleting from node1
  while (listTypeNext(li,&entry)) {
    if (listTypeEqual(&entry,graphEdge->key)) {
      listTypeDelete(&entry);
      break;
    }
  }
  listTypeReleaseIterator(li);
  // Deleting from node1 hash
  dictDeleteNoFree(node1->edges_hash, graphEdge->node2->key->ptr);

  // Deleting from node2
  li = listTypeInitIterator(graph->directed ? node2->incoming : node2->edges, 0, REDIS_TAIL);
  while (listTypeNext(li,&entry)) {
    if (listTypeEqual(&entry,graphEdge->key)) {
      listTypeDelete(&entry);
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
  graph->directed = 0;
  return graph;
}

GraphNode* GraphGetNode(Graph *graph, robj *key) {
  // TODO: To improve using a hash
  ListNode* current = graph->nodes->root;
  if (current == NULL)
    return NULL;
  while (current != NULL && ! equalStringObjects(key, ((GraphNode *)(current->value))->key ) ) {
    current = current->next;
  }
  if (current != NULL) {
    return (GraphNode *)(current->value);
  }
  return NULL;
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
  ListNode* current = graph->edges->root;

  if (current == NULL)
    return NULL;
  while (current != NULL) {
    GraphEdge *edge = (GraphEdge *)(current->value);
    if (equalStringObjects(key, edge->key)) {
      break;
    }
    current = current->next;
  }
  if (current != NULL) {
    return (GraphEdge *)(current->value);
  }
  return NULL;
}

ListNode* ListTail(List *list) {
  ListNode* current = list->root;
  if (current == NULL) return current;
  while(current->next != NULL) {
    current = current->next;
  }
  return current;
}

void ListAddNode(List *list, ListNode *node) {
  if (list->root == NULL) {
    list->root = node;
  } else {
    (ListTail(list))->next = node;
  }
  list->size++;
}

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

void dijkstra(redisClient *c, Graph *graph, GraphNode *node1, GraphNode *node2) {

  robj *distances_obj = createZsetObject();
  zset *distances = distances_obj->ptr;

  robj *visited = createZsetZiplistObject();
  robj *parents = createHashObject();

  robj **arr = (robj **)(zmalloc(20 * sizeof(robj *)));

  // Initialization
  zslInsert(distances->zsl, 0, node1->key);
  dictAdd(distances->dict, node1->key, NULL);

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
    zslDelete(distances->zsl, current_node_distance, current_node->key);
    dictDelete(distances->dict, current_node->key);

    // Marking the node as visited
    visited->ptr = zzlInsert(visited->ptr, current_node->key, 1);

    // Checking each of the neighbours
    //List *edges = graph->edges;
    //ListNode *current_list_node = edges->root;

    int neighbours_count = listTypeLength(current_node->edges);
    int j;

    for (j = 0; j < neighbours_count; j++) {

      listNode *ln = listIndex(current_node->edges->ptr, j);
      robj *edge_key = listNodeValue(ln);
      GraphEdge *edge = GraphGetEdgeByKey(graph, edge_key);
      GraphNode *neighbour = NULL;

      if (edge->node1 == current_node) {
        neighbour = edge->node2;
      } else if (edge->node2 == current_node) {
        neighbour = edge->node1;
      }

      if (neighbour != NULL) {

        // If neighbour already visited, skip
        if (zzlFind(visited->ptr, neighbour->key, NULL)) {
          continue;
        }

        float distance = edge->value + current_node_distance;
        float neighbour_distance;

        dictEntry *de;
        de = dictFind(distances->dict, neighbour->key);

        if (de != NULL) {
          neighbour_distance = *((float *)dictGetVal(de));
          if (distance < neighbour_distance) {
            // Deleting
            zslDelete(distances->zsl, neighbour_distance, neighbour->key);
            // Inserting again
            zslInsert(distances->zsl, distance, neighbour->key);
            // Update the parent
            hashTypeSet(parents, neighbour->key, current_node->key);

          }
        } else {
          zslInsert(distances->zsl, distance, neighbour->key);
          float *float_loc = zmalloc(sizeof(float));
          *float_loc = distance;
          dictAdd(distances->dict, neighbour->key, float_loc);
          hashTypeSet(parents, neighbour->key, current_node->key);
        }

      }
    }

    // READING MINIMUM
    // FOOTER
    zskiplistNode *first_node;
    first_node = distances->zsl->header->level[0].forward;

    GraphNode *previous_node = current_node;

    if (! finished && first_node != NULL) {
     current_node = GraphGetNode(graph, first_node->obj);
     current_node_distance = first_node->score;
    } else  {
      current_node = NULL;
    }

  }

  // Building reply
  GraphNode *cur_node = node2;
  int count = 1;
  unsigned char *vstr = NULL;
  unsigned int vlen = UINT_MAX;
  long long vll = LLONG_MAX;
  int ret;

  while(cur_node != NULL) {
    ret = hashTypeGetFromZiplist(parents, cur_node->key, &vstr, &vlen, &vll);
    robj *read = createStringObject(vstr, vlen);
    cur_node = GraphGetNode(graph, read);
    if (cur_node != NULL)
      count++;
    if (equalStringObjects(read, node1->key)) {
      break;
    }
  }

  addReplyMultiBulkLen(c, count + 1);
  cur_node = node2;

  robj **replies = zmalloc(sizeof(robj *) * count);
  int k = count - 1;
  replies[k--] = node2->key;

  while(cur_node != NULL) {
    ret = hashTypeGetFromZiplist(parents, cur_node->key, &vstr, &vlen, &vll);
    robj *read = createStringObject(vstr, vlen);
    cur_node = GraphGetNode(graph, read);
    if (cur_node != NULL) {
      replies[k--] = cur_node->key;
    }
    if (equalStringObjects(read, node1->key)) {
      break;
    }
  }
  redisAssert(k == -1);

  // Path nodes reversed
  for(k = 0; k < count; k++)
    addReplyBulk(c, replies[k]);

  zfree(replies);

  robj *distance_reply= createStringObjectFromLongDouble(final_distance);
  addReplyBulk(c, distance_reply);

  // Clear memory
  zfree(distances_obj->ptr);
  dictRelease(distances->dict);
  zslFree(distances->zsl);
  freeHashObject(parents);

  return REDIS_OK;
}

void gshortestpathCommand(redisClient *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);

  GraphNode *node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *node2 = GraphGetNode(graph_object, c->argv[3]);

  redisAssert(node1 != NULL);
  redisAssert(node2 != NULL);

  dijkstra(c, graph_object, node1, node2);

  return;
}

robj *createGraphObject() {
  Graph *ptr = GraphCreate();
  robj *obj = createObject(REDIS_GRAPH, ptr);
  obj->refcount = 100;
  obj->encoding = REDIS_GRAPH;
  return obj;
}

void gmintreeCommand(redisClient *c) {
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
    zslInsert(qzs->zsl, edge->value, edge->key);
  }

  // While the minimum edge connects existing node to new node, or BETTER: until the
  // new graph nodes length == graph 1 nodes length
  while (1) {
    zskiplistNode *node;
    node = qzs->zsl->header->level[0].forward;
    if (node != NULL) {
      GraphEdge *edge = GraphGetEdgeByKey(graph_object, node->obj);
      zslDelete(qzs->zsl, node->score, edge->key);
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
          // Check if the edge already exists in graph2
          GraphNode *tmp_edge = GraphGetEdgeByKey(graph2_object, edge_key);
          if (tmp_edge == NULL) {
            zslInsert(qzs->zsl, edge2->value, edge2->key);
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

void gsetdirectedCommand(redisClient *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyWrite(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  graph_object->directed = 1;

  RETURN_OK
}

void gvertexCommand(redisClient *c) {
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
  for (i = 2; i < c->argc; i++) {
    GraphNode *graph_node = GraphGetNode(graph_object, c->argv[i]);
  // TODO: Check if graph vertex already exists
    if (graph_node == NULL) {
      graph_node = GraphNodeCreate(c->argv[i], 0);
      GraphAddNode(graph_object, graph_node);
      added++;
    }
  }

  addReplyLongLong(c, added);
  return REDIS_OK;
}

void gincomingCommand(redisClient *c) {
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

  return REDIS_OK;
}

void gneighboursCommand(redisClient *c) {
  robj *graph;
  robj *edge_key;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *node = GraphGetNode(graph_object, c->argv[2]);

  // Neighbours count
  long count = listTypeLength(node->edges);
  addReplyMultiBulkLen(c, count);
  int i;
  robj *list = node->edges;
  for (i = 0; i < count; i++) {
    edge_key = listNodeValue(listIndex(list->ptr, i));
    edge = GraphGetEdgeByKey(graph_object, edge_key);
    if (equalStringObjects(edge->node1->key, node->key)) {
      addReplyBulk(c, edge->node2->key);
    } else {
      addReplyBulk(c, edge->node1->key);
    }
  }

  return REDIS_OK;
}

robj *neighboursToSet(GraphNode *node, Graph *graph_object) {

  // Creating neighbours set of the first node

  GraphEdge *edge;
  robj *edge_key;
  robj *set = NULL;

  set = setTypeCreate(node->key);
  //return set;
  //return set;

  long count;
  count = listTypeLength(node->edges);
  robj *list = node->edges;
  int i;
  for (i = 0; i < count; i++) {
    edge_key = listNodeValue(listIndex(list->ptr, i));
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
    setTypeAdd(set, neighbour_key);
  }

  return set;
}

void gcommonCommand(redisClient *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *node2 = GraphGetNode(graph_object, c->argv[3]);
  robj *set1 = NULL;
  robj *set2 = NULL;

  set1 = neighboursToSet(node1, graph_object);
  set2 = neighboursToSet(node2, graph_object);

  if (set1 == NULL || set2 == NULL) {
    addReplyMultiBulkLen(c, 0);
    return REDIS_OK;
  }

  robj *result = createIntsetObject();

  // Switch set1, and set2 if set1 length is bigger. To improve performance
  robj *temp;
  if (setTypeSize(set1) > setTypeSize(set2)) {
    temp = set2;
    set2 = set1;
    set1 = temp;
  }

  setTypeIterator *si = setTypeInitIterator(set1);
  int encoding;
  int intobj;
  robj *eleobj;

  while((encoding = setTypeNext(si,&eleobj,&intobj)) != -1) {
    if (setTypeIsMember(set2, eleobj)){
      setTypeAdd(result, eleobj);
    }
  }
  setTypeReleaseIterator(si);

  addReplyMultiBulkLen(c, setTypeSize(result));

  si = setTypeInitIterator(set1);
  while((encoding = setTypeNext(si,&eleobj,&intobj)) != -1) {
    addReplyBulk(c, eleobj);
  }
  setTypeReleaseIterator(si);

  freeSetObject(set1);
  freeSetObject(set2);
  freeSetObject(result);

  return REDIS_OK;
}

void gedgeexistsCommand(redisClient *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);
  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *graph_node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *graph_node2 = GraphGetNode(graph_object, c->argv[3]);

  // Check whether the edge already exists
  edge = NULL;
  if (graph_node1 != NULL && graph_node2 != NULL)
    edge = GraphGetEdge(graph_object, graph_node1, graph_node2);

  if (edge != NULL) {
    addReply(c, shared.cone);
  } else {
    addReply(c, shared.czero);
  }
  return REDIS_OK;
}

void gedgeCommand(redisClient *c) {
  robj *graph;
  GraphEdge *edge;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);

  Graph *graph_object = (Graph *)(graph->ptr);

  if (equalStringObjects(c->argv[2], c->argv[3])) {
    addReply(c, shared.czero);
    return REDIS_OK;
  }

  GraphNode *graph_node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *graph_node2 = GraphGetNode(graph_object, c->argv[3]);

  // Check whether the edge already exists
  edge = GraphGetEdge(graph_object, graph_node1, graph_node2);

  char *value_string = c->argv[4]->ptr;
  float value_float = atof(value_string);

  if (edge != NULL) {
    edge->value = value_float;
    addReply(c, shared.czero);
    return REDIS_OK;
  } else {
    edge = GraphEdgeCreate(graph_node1, graph_node2, value_float);
    GraphAddEdge(graph_object, edge);

    robj *value;
    addReply(c, shared.cone);
    return REDIS_OK;
  }
}

void gedgeremCommand(redisClient *c) {
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

  return REDIS_OK;
}

void gedgeincrbyCommand(redisClient *c) {
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
    return REDIS_OK;
  } else {
    gedgeCommand(c);
  }
}

void gverticesCommand(redisClient *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);

  if (graph == NULL || checkType(c, graph, REDIS_GRAPH)) {
    addReply(c, shared.czero);
    return;
  }

  Graph *graph_object = (Graph *)(graph->ptr);

  List *graphNodes = graph_object->nodes;
  ListNode *current_node = graphNodes->root;

  int count = graphNodes->size;
  addReplyMultiBulkLen(c, count);

  robj *reply = createStringObject("Done", strlen("Done"));

  current_node = graphNodes->root;
  while (current_node != NULL) {
    GraphNode *graphNode = (GraphNode *)(current_node->value);
    addReplyBulk(c, graphNode->key);
    current_node = current_node->next;
  }

  return REDIS_OK;
}

void gedgesCommand(redisClient *c) {
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
    robj *reply3 = createStringObjectFromLongDouble(graphEdge->value);
    addReplyBulk(c, reply3);
    current_node = current_node->next;
  }

  return REDIS_OK;
}

void testCommand(redisClient *c) {

  // Writing and reading from a hash
  dict *d = dictCreate(&dbDictType, NULL);
  robj *key = createStringObject("key", strlen("key"));
  sds key_str = sdsdup(key->ptr);
  robj *value = createStringObject("omar", strlen("omar"));
  redisAssert(dictAdd(d, key_str, value) == DICT_OK);
  dictEntry *entry = dictFind(d,key->ptr);
  robj *get_value = (robj *)(dictGetVal(entry));
  redisAssert(equalStringObjects(get_value, value));

  RETURN_OK
}
