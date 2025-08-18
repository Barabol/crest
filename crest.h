#ifndef __CREST__
#define __CREST__

#define CREST_REUSE_SOCKET

#define CREST_USE_LOGGER_COLOR

#define CREST_RANDOM_SLOGAN

#define CREST_LOG_CONNECTIONS

#define CREST_PORT 8080

/**
 * maximal ammount of pending connections
 */
#define CREST_MAX_CONNECTIONS 100

#define CREST_MAX_THREADS 4

#define CREST_VERSION "0.1"

/**
 * size of read buffer
 */
#define CREST_MAX_REQUEST_LENGTH 8192

/**
 * maximal name length for query varable
 */
#define CREST_MAX_QUERY_NAME_LEN 1024

/**
 * maximal value length for query varable
 */
#define CREST_MAX_QUERY_VALUE_LEN 2048

/**
 * maximal length of header name
 */
#define CREST_MAX_HEADER_NAME_LEN 1024

/**
 * maximal length of header value
 */
#define CREST_MAX_HEADER_VALUE_LEN 1024

/**
 * Array: CrestWdayNames
 *\---------------------
 *
 * short names of week days
 */
static const char CrestWdayNames[7][4] = {"Mon", "Tue", "Wed", "Thu",
                                          "Fri", "Sat", "Sun"};

/**
 * Array: CrestMdayNames
 *\---------------------
 *
 * short names of months
 */
static const char CrestMdayNames[12][4] = {"Jan", "Feb", "Mar", "Apr",
                                           "May", "Jun", "Jul", "Aug",
                                           "Sep", "Oct", "Nov", "Dec"};
/**
 * Array: CrrestCTNames
 *\---------------------
 *
 * names of content types
 */
static const char CrestCTNames[][25] = {"application/json", "text/html"};

#ifdef CREST_RANDOM_SLOGAN
/**
 * Array: CrrestSlogans
 *\---------------------
 *
 * slogans displayed below crest logo
 */
static const char CrestSlogans[][27] = {
    "Better than nothing.     ", "Could be worse, trust me.",
    "Good enough to pass.     ", "Sometimes works.         ",
    "Maybe memory safe.       ", "C stands for caution.    ",
    "                         "};
#endif

/**
 * enum: CrestContentType
 *\----------------------
 *
 * enum of all possible response content types
 */
typedef enum : unsigned {
   CREST_CONTENT_JSON = 0, // application/json
   CREST_CONTENT_HTML = 1, // text/html
} CrestContentType;

/**
 * enum: CrestRequestType
 *\----------------------
 *
 * enum of all possible requests
 */
typedef enum : char {
   CREST_GET = 0,     // request: GET
   CREST_HEAD = 1,    // request: HEAD
   CREST_PUT = 2,     // request: PUT
   CREST_POST = 3,    // request: POST
   CREST_DELETE = 4,  // request: DELETE
   CREST_OPTIONS = 5, // request: OPTIONS
   CREST_TRACE = 6,   // request: TRACE
   CREST_CONNECT = 7, // request: CONNECT
   CREST_PATCH = 8,   // request: PATCH
} CrestRequestType;

/**
 * Enum: CrestResponseCode
 * \-----------------------
 *
 * response codes
 */
typedef enum : unsigned {
   CREST_RES_OK = 200,        // 200
   CREST_RES_NOT_FOUND = 404, // 404
} CrestResponseCode;

/**
 * Struct: CrestTree
 * \-----------------
 *
 * base structure for trie tree
 */
typedef struct __CrestTree__ {
   struct __CrestTree__ *children[128];
   /**
    * 0 - headers
    *
    * 1 - path varables
    *
    * 2 - query varables
    */
   char *value[3];
} CrestTree;

/**
 * Struct: CrestResponse
 * \---------------------
 *
 * base structure for API response
 */
typedef struct {
   const char *content;
   CrestContentType type;
   unsigned code;
} CrestResponse;

/**
 * Struct: CrestRequest
 * \--------------------
 *
 * base structure for API request
 */
typedef struct {
   const char *content;
   int clientSocket;
   CrestRequestType requestType;

   // holds path varables, headers and query varables
   CrestTree *vars;
   const char *ip;
} CrestRequest;

/**
 * Function: crestStart
 * \-------------------
 *
 * function responsible for initialization of
 *
 * rest API
 */
void crestStart(int argc, char **argv);

/**
 * Function: crestAddHandler
 * \-------------------------
 *
 * adds new API endpoint
 *
 * must be used before crestStart
 *
 * returns 0 if successful
 *
 * you can define path varables
 *
 * - %s as string
 *
 * - %d as decimal type
 *
 * eg.
 *
 * "/user/get/%d<id>"
 */
int crestAddHandler(CrestResponse *(*func)(CrestRequest *),
                    CrestRequestType type, const char *path);
/**
 * Function: crestGenResponse
 * \--------------------------
 *
 * generates response structure
 */
CrestResponse *crestGenResponse(unsigned code, const char *content);

/**
 * Function: crestGetVar
 * \---------------------
 *
 * returns path varable with provided name
 *
 * if does not exist returns empty string
 */
const char *crestGetVar(CrestRequest *req, const char *name);

/**
 * Function: crestGetQuery
 * \-----------------------
 *
 * returns query with provided name
 *
 * if does not exist returns empty string
 */
const char *crestGetQuery(CrestRequest *req, const char *name);

/**
 * Function: crestGetHeader
 * \------------------------
 *
 * returns header with provided name
 *
 * if does not exist returns empty string
 */
const char *crestGetHeader(CrestRequest *req, const char *name);

/**
 * Function: crestGetVarPtr
 * \------------------------
 *
 * returns path varable with provided name
 *
 * if does not exist returns NULL
 */
const char *crestGetVarPtr(CrestRequest *req, const char *name);

/**
 * Function: crestGetQueryPtr
 * \--------------------------
 *
 * returns query with provided name
 *
 * if does not exist returns NULL
 */
const char *crestGetQueryPtr(CrestRequest *req, const char *name);

/**
 * Function: crestGetHeaderPtr
 * \---------------------------
 *
 * returns header with provided name
 *
 * if does not exist returns NULL
 */
const char *crestGetHeaderPtr(CrestRequest *req, const char *name);
#endif
