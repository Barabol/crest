#ifndef __CREST__
#define __CREST__

#define CREST_REUSE_SOCKET

#define CREST_USE_LOGGER_COLOR

#define CREST_RANDOM_SLOGAN

#define CREST_LOG_CONNECTIONS

#define CREST_THREAD_OVERLOAD_LOG

#define CREST_WARN_ON_REUSE

#define CREST_PORT 8080

/**
 * maximal ammount of pending connections
 */
#define CREST_MAX_CONNECTIONS 100

#define CREST_MAX_THREADS 4

#define CREST_VERSION "0.5.1 BETA"

/**
 * size of initial read buffer
 *
 * if request is longer than that buffer will be allocated on heap
 */
#define CREST_INITIAL_REQUEST_LENGTH 8000

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
 * maximal length of header name
 */
#define CREST_MAX_COOKIE_NAME_LEN 256

/**
 * maximal length of header value
 */
#define CREST_MAX_COOKIE_VALUE_LEN 256

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
   CREST_CONTENT_JSON = 0,   // application/json
   CREST_CONTENT_HTML = 1,   // text/html
   CREST_CONTENT_X_FORM = 2, // X-Form
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
 * Enum: CrestResponseFlags
 * \------------------------
 *
 * response flags
 */
typedef enum : unsigned {
   CREST_RES_F_FREE_RES_BODY = 1, // free response body after sending response
} CrestResponseFlags;

/**
 * Struct: BTreeLeaf
 * \--------------------
 *
 * base structure for binary tree leaf
 */
typedef struct __btreenode__ {
   struct __btreenode__ *l, *r;
   unsigned key;
   char *value;
   char *keyStr;
   int len;
} BTreeLeaf;

/**
 * Struct: Set
 * \--------------------
 *
 * base structure for set that holds headers, query vars and path vars
 */
typedef struct {
   unsigned long elements;
   BTreeLeaf *tree;
} Set;

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
   Set *headers;
   Set *cookies;
   int flags;
} CrestResponse;

/**
 * Struct: CrestRequest
 * \--------------------
 *
 * base structure for API request
 */
typedef struct {
   const char *content;
   long contentLen;
   int clientSocket;
   CrestRequestType requestType;

   /**
    * holds path varables, headers and query varables
    *
    * 0 - headers
    *
    * 1 - path varables
    *
    * 2 - query varables
    *
    * 3 - cookies (by default will be NULL)
    */
   Set *vars[4];
   const char *ip;
} CrestRequest;

/**
 * Macro: crestSession
 * \-------------------
 *
 * sets new session object if session is established else it returns session
 * object for current session
 *
 */
#define crestSession(SessionObject, req, res, setter)                          \
   ({                                                                          \
      SessionObject *crest_session = (SessionObject *)crestGetSession(req);    \
      if (!crest_session) {                                                    \
         crest_session = (SessionObject *)malloc(sizeof(SessionObj));          \
         setter(crest_session);                                                \
         crestSetSession(res, (void *)crest_session);                          \
      }                                                                        \
      crest_session;                                                           \
   })

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
 * Function: crestGenResponseF
 * \---------------------------
 *
 * generates response structure with flags
 */
CrestResponse *crestGenResponseF(unsigned code, const char *content, int flags);

/**
 * Function: crestGetVar
 * \---------------------
 *
 * returns path varable with provided name
 *
 * if does not exist returns empty string
 */
const char *crestGetVar(CrestRequest *req, char *name);

/**
 * Function: crestGetQuery
 * \-----------------------
 *
 * returns query with provided name
 *
 * if does not exist returns empty string
 */
const char *crestGetQuery(CrestRequest *req, char *name);

/**
 * Function: crestGetHeader
 * \------------------------
 *
 * returns header with provided name
 *
 * if does not exist returns empty string
 */
const char *crestGetHeader(CrestRequest *req, char *name);

/**
 * Function: crestGetVarPtr
 * \------------------------
 *
 * returns path varable with provided name
 *
 * if does not exist returns NULL
 */
const char *crestGetVarPtr(CrestRequest *req, char *name);

/**
 * Function: crestGetQueryPtr
 * \--------------------------
 *
 * returns query with provided name
 *
 * if does not exist returns NULL
 */
const char *crestGetQueryPtr(CrestRequest *req, char *name);

/**
 * Function: crestGetHeaderPtr
 * \---------------------------
 *
 * returns header with provided name
 *
 * if does not exist returns NULL
 */
const char *crestGetHeaderPtr(CrestRequest *req, char *name);

/**
 * Function: crestSetHeader
 * \------------------------
 *
 * sets header with provided name
 *
 * returns 0 if successful
 */
int crestSetHeader(CrestResponse *res, char *name, char *value);

/**
 * Function: crestSetCookie
 * \------------------------
 *
 * sets cookie with provided name
 *
 * returns 0 if successful
 */
int crestSetCookie(CrestResponse *res, char *name, char *value);

/**
 * Function: crestDropCookie
 * \------------------------
 *
 * deletes cookie with provided name
 *
 * returns 0 if successful
 */
int crestDropCookie(CrestResponse *res, char *name);

/**
 * Function: crestGetCookie
 * \-------------------------
 *
 * returns value of request cookie
 *
 * if it does not exists returns empty string
 */
const char *crestGetCookie(CrestRequest *req, char *name);

/**
 * Function: crestGetCookiePtr
 * \-------------------------
 *
 * returns value of request cookie
 *
 * if it does not exists returns NULL
 */
const char *crestGetCookiePtr(CrestRequest *req, char *name);

/**
 * Function: crestGetSession
 * \-------------------------
 *
 * returns void ptr to user defined session object
 */
void *crestGetSession(CrestRequest *req);

/**
 * Function: crestSetSession
 * \-------------------------
 *
 * sets user defined session object
 *
 * returns sessionId
 */
int crestSetSession(CrestResponse *res, void *sessionObj);

/**
 * Function: crestSetSessionDropFunc
 * \---------------------------------
 *
 * sets function for freeing memory of session object
 *
 * provided function must return 0 if on success and not 0 on error
 *
 * SHOULD be called only once in main before starting crest
 */
int crestSetSessionDropFunc(int (*func)(void *));

/**
 * Function: crestDropSession
 * \-------------------------
 *
 * returns void ptr to user defined session object
 */
int crestDropSession(CrestRequest *req);

#endif
