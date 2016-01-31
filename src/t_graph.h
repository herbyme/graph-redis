typedef struct ListNode {
  void* value;
  struct ListNode *next;

} ListNode;

typedef struct {
  ListNode *root;
  ListNode *tail;
  int size;
} List;

typedef struct {
  float value;
  robj *key;
  robj *edges;
  dict *edges_hash;
  robj *incoming; // Only for directed graphs
  robj *memory_key;
  int visited; // TEMP
  void *parent;
} GraphNode;

typedef struct {
  GraphNode *node1;
  GraphNode *node2;
  float value;
  robj *memory_key;
} GraphEdge;

typedef struct {
  List *nodes; //TODO: Use redis lists like the GraphNode
  List *edges; //TODO: Use redis lists like the GraphNode
  robj *nodes_hash;
  char directed;
} Graph;

robj *createGraphObject();
GraphNode* GraphNodeCreate(robj *key, float value);
void GraphAddNode(Graph *graph, GraphNode *node);
GraphNode* GraphGetNode(Graph *graph, robj *key);
GraphEdge* GraphEdgeCreate(GraphNode *node1, GraphNode *node2, float value);
void GraphAddEdge(graph2_object, new_edge);
