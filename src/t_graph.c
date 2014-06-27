#include "redis.h"
#include <math.h> /* isnan(), isinf() */

#define RETURN_OK \
  robj* value10 = createStringObject("OK", strlen("OK")); \
  addReplyBulk(c, value10); \

#define RETURN_REPLY \
  addReplyBulk(c, reply); \
  return REDIS_OK;

#define RETURN_CANCEL \
  robj* value20 = createStringObject("Cancel", strlen("Cancel")); \
  addReplyBulk(c, value20); \
  return REDIS_OK;

typedef struct ListNode {
  void* value;
  struct ListNode *next;

} ListNode;

typedef struct {
  ListNode *root;
} List;

typedef struct {
  float value;
  robj *key;
} GraphNode;

typedef struct {
  GraphNode *node1;
  GraphNode *node2;
  float value;
} GraphEdge;

typedef struct {
  List *nodes;
  List *edges;
} Graph;

ListNode* ListNodeCreate(void* value) {
  ListNode* listNode = (ListNode *)zmalloc(sizeof(ListNode));
  listNode->value = value;
  listNode->next = NULL;
  return listNode;
}

List* ListCreate() {
  List* list = zmalloc(sizeof(List));
  list->root = NULL;
  return list;
}

GraphNode* GraphNodeCreate(robj *key, float value) {
  GraphNode* graphNode = zmalloc(sizeof(GraphNode));
  char *new_name = zmalloc(strlen(key->ptr));
  strcpy(new_name, key->ptr);
  graphNode->key = createStringObject(new_name, strlen(new_name));
  graphNode->key->refcount = 100;
  graphNode->value = value;
  return graphNode;
}

GraphEdge* GraphEdgeCreate(GraphNode *node1, GraphNode *node2, float value) {
  GraphEdge* graphEdge = zmalloc(sizeof(GraphEdge));
  graphEdge->node1 = node1;
  graphEdge->node2 = node2;
  graphEdge->value = value;
  return graphEdge;
}

void GraphAddNode(Graph *graph, GraphNode *node) {
  ListNode* listNode = ListNodeCreate((void *)(node));
  ListAddNode(graph->nodes, listNode);
}

void GraphAddEdge(Graph *graph, GraphEdge *graphEdge) {
  ListNode* listNode = ListNodeCreate((void *)(graphEdge));
  ListAddNode(graph->edges, listNode);
}

Graph* GraphCreate() {
  Graph* graph = zmalloc(sizeof(Graph));
  graph->nodes = ListCreate();
  graph->edges = ListCreate();
  return graph;
}

GraphNode* GraphGetNode(Graph *graph, robj *key) {
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
}

typedef struct HashObject {
  void *key;
  void *value;
} HashObject;

typedef struct Hash {
  List* nodes;
} Hash;

Hash* HashCreate() {
  Hash* hash = zmalloc(sizeof(Hash));
  hash->nodes = ListCreate();
  return hash;
}

void HashSetObject(Hash *hash, void *key, void *value) {
  HashObject* hashObject = zmalloc(sizeof(HashObject));
  hashObject->key = key;
  hashObject->value = value;
  ListNode* listNode = ListNodeCreate(hashObject);
  ListAddNode(hash->nodes, listNode);
}

void* HashGetObject(Hash *hash, void *key) {
  ListNode* current = hash->nodes->root;
  while (current != NULL && ((HashObject *)(current->value))->key != key) {
    current = current->next;
  }
  if (current != NULL) {
    return ((HashObject *)(current->value))->value;
  } else {
    return NULL;
  }
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

    // Checking each of the neighbours
    List *edges = graph->edges;
    ListNode *current_list_node = edges->root;

    while(current_list_node != NULL) {

      // Marking the node as visited
      visited->ptr = zzlInsert(visited->ptr, current_node->key, 1);

      GraphEdge *edge = (GraphEdge *)(current_list_node->value);
      GraphNode *neighbour = NULL;

      if (edge->node1 == current_node) {
        neighbour = edge->node2;
      } else if (edge->node2 == current_node) {
        neighbour = edge->node1;
      }

      if (neighbour != NULL) {

        // If neighbour already visited, skip
        if (zzlFind(visited->ptr, neighbour->key, NULL)) {
          current_list_node = current_list_node->next;
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
      current_list_node = current_list_node->next;
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
    count++;
    if (equalStringObjects(read, node1->key)) {
      break;
    }
  }

  addReplyMultiBulkLen(c, count + 1);
  cur_node = node2;
  addReplyBulk(c, node2->key);

  while(cur_node != NULL) {
    ret = hashTypeGetFromZiplist(parents, cur_node->key, &vstr, &vlen, &vll);
    robj *read = createStringObject(vstr, vlen);
    cur_node = GraphGetNode(graph, read);
    if (cur_node != NULL)
      addReplyBulk(c, cur_node->key);
    if (equalStringObjects(read, node1->key)) {
      break;
    }
  }

  robj *distance_reply= createStringObjectFromLongDouble(final_distance);
  addReplyBulk(c, distance_reply);

  return REDIS_OK;
}

void shortestpathCommand(redisClient *c) {

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
  return obj;
}

void gvertixCommand(redisClient *c) {

  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyWrite(c->db, key);
  robj *value;
  if (graph == NULL) {
    graph = createGraphObject();
    dbAdd(c->db, key, graph);
  }

  // Check if graph vertex already exists

  Graph *graph_object = (Graph *)(graph->ptr);
  GraphNode *graph_node = GraphGetNode(graph_object, c->argv[2]);

  if (graph_node == NULL ) {
    graph_node = GraphNodeCreate(c->argv[2], 0);
    GraphAddNode(graph_object, graph_node);
    addReply(c, shared.cone);
  } else {
    addReply(c, shared.czero);
  }
  return REDIS_OK;
}

void gedgeCommand(redisClient *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);

  Graph *graph_object = (Graph *)(graph->ptr);

  GraphNode *graph_node1 = GraphGetNode(graph_object, c->argv[2]);
  GraphNode *graph_node2 = GraphGetNode(graph_object, c->argv[3]);

  char *value_string = c->argv[4]->ptr;
  float value_float = atof(value_string);

  GraphEdge *edge = GraphEdgeCreate(graph_node1, graph_node2, value_float);
  GraphAddEdge(graph_object, edge);

  robj *value;
  value = createStringObject("ok", strlen("ok"));
  addReplyBulk(c, value);
  return REDIS_OK;
}

void listgraphnodesCommand(redisClient *c) {
  robj *graph;
  robj *key = c->argv[1];
  graph = lookupKeyRead(c->db, key);

  Graph *graph_object = (Graph *)(graph->ptr);

  List *graphNodes = graph_object->nodes;
  ListNode *current_node = graphNodes->root;

  int count = 0;
  while (current_node != NULL) {
    count++;
    current_node = current_node->next;
  }

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

void listgraphedgesCommand(redisClient *c) {
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
