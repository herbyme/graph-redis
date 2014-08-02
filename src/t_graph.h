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
  int size;
} List;

typedef struct {
  float value;
  robj *key;
  robj *edges;
  dict *edges_hash;
  robj *incoming; // Only for directed graphs
} GraphNode;

typedef struct {
  GraphNode *node1;
  GraphNode *node2;
  float value;
  robj *key;
} GraphEdge;

typedef struct {
  List *nodes; //TODO: Use redis lists like the GraphNode
  List *edges; //TODO: Use redis lists like the GraphNode
  char directed;
} Graph;

robj *createGraphObject();
GraphNode* GraphNodeCreate(robj *key, float value);
void GraphAddNode(Graph *graph, GraphNode *node);
GraphNode* GraphGetNode(Graph *graph, robj *key);
GraphEdge* GraphEdgeCreate(GraphNode *node1, GraphNode *node2, float value);
void GraphAddEdge(graph2_object, new_edge);
