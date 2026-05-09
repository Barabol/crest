#include "crest.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef enum : char {
   C_NONE,
   C_STRING,
   C_DECIMAL,
} PathVarType;

typedef struct __PathTree__ {
   struct __PathTree__ *children[128];
   struct __PathTree__ *var;
   CrestResponse *(*func[9])(CrestRequest *);
   struct {
      char *name;
      PathVarType type;
   } pathVar;
} PathTree;

// TODO: maybe change to Set
typedef struct __TrieTree__ {
   struct __TrieTree__ *children[128];
   char value;
} TrieTree;

typedef struct {
   pthread_t thr;
   int client;
   char used;
   char running;
   int id;
   const char *ip;
} Thread;

// unfortunetly if i don't want to pass
// pathTree as argument to all functions
// i have to declare it in global scope
// i hope it won't complicate my
// multithreading plana
PathTree *pathTree = NULL;

TrieTree *requests = NULL;

Thread threads[CREST_MAX_THREADS];

unsigned cntLenHash = 0;

unsigned cookieHash = 0;

unsigned setCookieHash = 0;

static Set *sessions = NULL;
int (*sessionFreeFunc)(void *) = NULL;
sem_t sessionSem;

typedef enum {
   INFO = 0,
   MINOR = 1,
   CRITICAL = 2,
} LogLevel;

void logInfo(LogLevel level, const char *content, ...) {
   va_list list;
   va_start(list, content);
   static const char *names[] = {"   INFO   ", "  MINOR   ", " CRITICAL "};
   time_t timer;
   time(&timer);
   struct tm *gmtTimer = gmtime(&timer);

#ifndef CREST_USE_LOGGER_COLOR
   printf("[%d-%02d-%02d|%02d:%02d:%02d] [%s] --- ", gmtTimer->tm_year + 1900,
          gmtTimer->tm_mon + 1, gmtTimer->tm_mday, gmtTimer->tm_hour,
          gmtTimer->tm_min, gmtTimer->tm_sec, names[level]);
#endif

#ifdef CREST_USE_LOGGER_COLOR
   static const char *colors[] = {"\033[34m", "\033[33m", "\033[31m"};
   printf("%s[%d-%02d-%02d|%02d:%02d:%02d] \033[1m[%s]\033[0m%s --- ",
          colors[level], gmtTimer->tm_year + 1900, gmtTimer->tm_mon + 1,
          gmtTimer->tm_mday, gmtTimer->tm_hour, gmtTimer->tm_min,
          gmtTimer->tm_sec, names[level], colors[level]);
#endif

   vprintf(content, list);

#ifdef CREST_USE_LOGGER_COLOR
   printf("\033[0m\n");
#endif

#ifndef CREST_USE_LOGGER_COLOR
   printf("\n");
#endif
}

static inline unsigned murmur_32_scramble(unsigned k) {
   k *= 0xcc9e2d51;
   k = (k << 15) | (k >> 17);
   k *= 0x1b873593;
   return k;
}

unsigned murmur3_32(const char *key, size_t len) {
   unsigned h = 6792341;
   unsigned k;
   for (size_t i = len >> 2; i; i--) {
      memcpy(&k, key, sizeof(unsigned));
      key += sizeof(unsigned);
      h ^= murmur_32_scramble(k);
      h = (h << 13) | (h >> 19);
      h = h * 5 + 0xe6546b64;
   }
   k = 0;
   for (size_t i = len & 3; i; i--) {
      k <<= 8;
      k |= key[i - 1];
   }
   h ^= murmur_32_scramble(k);
   h ^= len;
   h ^= h >> 16;
   h *= 0x85ebca6b;
   h ^= h >> 13;
   h *= 0xc2b2ae35;
   h ^= h >> 16;
   return h;
}

Set *setCreate() {
   Set *set = (Set *)malloc(sizeof(Set));
   if (!set) {
      logInfo(CRITICAL, "unable to allocate memory");
      return NULL;
   }
   set->tree = NULL;
   set->elements = 0;
   return set;
}
#define newLeaf()                                                              \
   leaf = (BTreeLeaf *)malloc(sizeof(BTreeLeaf));                              \
   if (leaf == NULL) {                                                         \
      logInfo(CRITICAL, "unable to allocate memory");                          \
      return NULL;                                                             \
   }                                                                           \
   leaf->l = NULL;                                                             \
   leaf->r = NULL;                                                             \
   int len_ = strlen(key) + 1;                                                 \
   leaf->key = hash;                                                           \
   leaf->keyStr = (char *)malloc(sizeof(char) * len_);                         \
   if (leaf->keyStr == NULL) {                                                 \
      logInfo(CRITICAL, "unable to allocate memory");                          \
      return NULL;                                                             \
   }                                                                           \
   int x;                                                                      \
   for (x = 0; key[x]; x++)                                                    \
      leaf->keyStr[x] = key[x];                                                \
   leaf->keyStr[x] = 0;                                                        \
   leaf->len = len_;                                                           \
   len_ = strlen(value) + 1;                                                   \
   leaf->len += len_;                                                          \
   leaf->value = (char *)malloc(sizeof(char) * len_);                          \
   if (leaf->value == NULL) {                                                  \
      logInfo(CRITICAL, "unable to allocate memory");                          \
      return NULL;                                                             \
   }                                                                           \
   for (x = 0; value[x]; x++)                                                  \
      leaf->value[x] = value[x];                                               \
   leaf->value[x] = 0;

unsigned setAdd(Set *set, char *key, char *value) {
   unsigned long len = strlen(key);
   unsigned hash = murmur3_32(key, len);
   BTreeLeaf *leaf = NULL;
   if (set->tree == NULL) {
      newLeaf();
      set->tree = leaf;
      set->elements++;
      return hash;
   }
   BTreeLeaf *node = set->tree;
   BTreeLeaf *prNode = NULL;
   char lor = 0;
   unsigned holder;
   while (node) {
      holder = node->key;
      if (hash == holder) {
         // printf("-> %s\n", node->value);
         node->len -= strlen(node->value);
         free(node->value);
         int len_ = strlen(value);
         node->len += len_;
         node->value = (char *)malloc(sizeof(char) * (len_ + 1));
         int x;
         for (x = 0; value[x]; x++)
            node->value[x] = value[x];
         node->value[x] = 0;
         return hash;
      }
      if (hash < holder) {
         prNode = node;
         lor = 0;
         node = node->l;
      } else {
         prNode = node;
         lor = 1;
         node = node->r;
      }
   }
   newLeaf();
   if (lor)
      prNode->r = leaf;
   else
      prNode->l = leaf;
   set->elements++;
   return hash;
}

unsigned setAddSession(Set *set, unsigned hash, void *value) {
   BTreeLeaf *leaf = NULL;
   if (set->tree == NULL) {
      leaf = (BTreeLeaf *)malloc(sizeof(BTreeLeaf));
      if (leaf == ((void *)0)) {
         logInfo(CRITICAL, "unable to allocate memory");
         return 0;
      }
      leaf->l = NULL;
      leaf->r = NULL;
      leaf->key = hash;
      leaf->keyStr = NULL;
      leaf->value = (char *)value;
      set->tree = leaf;
      set->elements++;
      return hash;
   }
   BTreeLeaf *node = set->tree;
   BTreeLeaf *prNode = NULL;
   char lor = 0;
   unsigned holder;
   while (node) {
      holder = node->key;
      if (hash == holder) {
         if (sessionFreeFunc)
            sessionFreeFunc(node->value);
         node->value = (char *)value;
         return hash;
      }
      if (hash < holder) {
         prNode = node;
         lor = 0;
         node = node->l;
      } else {
         prNode = node;
         lor = 1;
         node = node->r;
      }
   }
   leaf = (BTreeLeaf *)malloc(sizeof(BTreeLeaf));
   if (leaf == ((void *)0)) {
      logInfo(CRITICAL, "unable to allocate memory");
      return 0;
   }
   leaf->l = NULL;
   leaf->r = NULL;
   leaf->key = hash;
   leaf->keyStr = NULL;
   leaf->value = (char *)value;
   if (lor)
      prNode->r = leaf;
   else
      prNode->l = leaf;
   set->elements++;
   return hash;
}

const char *setGet(Set *set, unsigned key) {
   if (set == NULL || set->tree == NULL)
      return NULL;
   BTreeLeaf *node = set->tree;
   while (node) {
      if (node->key == key)
         return node->value;
      if (node->key > key)
         node = node->l;
      else
         node = node->r;
   }
   return NULL;
}

const char *setGetByName(Set *set, char *key) {
   unsigned long len = strlen(key);
   unsigned hash = murmur3_32(key, len);
   if (set == NULL || set->tree == NULL) {
      return NULL;
   }
   BTreeLeaf *node = set->tree;
   while (node) {
      if (node->key == hash)
         return node->value;
      if (node->key > hash)
         node = node->l;
      else
         node = node->r;
   }
   return NULL;
}

BTreeLeaf *setGetLeaf(Set *set, unsigned key) {
   if (set == NULL || set->tree == NULL)
      return NULL;
   BTreeLeaf *node = set->tree;
   while (node) {
      if (node->key == key)
         return node;
      if (node->key > key)
         node = node->l;
      else
         node = node->r;
   }
   return NULL;
}

void freeLeafs(BTreeLeaf *leaf) {
   if (leaf == NULL)
      return;
   freeLeafs(leaf->l);
   freeLeafs(leaf->r);
   if (leaf->value)
      free(leaf->value);
   if (leaf->keyStr)
      free(leaf->keyStr);
   free(leaf);
}
unsigned long LeafsLen(BTreeLeaf *leaf) {
   if (leaf == NULL)
      return 0;
   return leaf->len + LeafsLen(leaf->l) + LeafsLen(leaf->r) + 3;
}

unsigned long setGetLen(Set *set) {
   if (set == NULL) {
      return 0;
   }
   return LeafsLen(set->tree);
}

void setFree(Set *set) {
   if (set == NULL)
      return;
   freeLeafs(set->tree);
   free(set);
}

void freeLeafsSession(BTreeLeaf *leaf) {
   if (leaf == NULL)
      return;
   freeLeafsSession(leaf->l);
   freeLeafsSession(leaf->r);
   if (leaf->value && sessionFreeFunc) {
      if (sessionFreeFunc(leaf->value)) {
         logInfo(CRITICAL, "unable to free session object");
      } else {
         leaf->value = NULL;
      }
   }
   free(leaf);
}

void setFreeSession(Set *set) {
   if (set == NULL)
      return;
   freeLeafsSession(set->tree);
   free(set);
}

void leafPush(BTreeLeaf *leaf, char *buf) {
   if (leaf == NULL)
      return;
   sprintf(buf, "%s\n%s: %s", buf, leaf->keyStr, leaf->value);
   leafPush(leaf->l, buf);
   leafPush(leaf->r, buf);
}

void setPush(Set *set, char *buf) {
   if (set == NULL || buf == NULL)
      return;
   leafPush(set->tree, buf);
}

int addRequest(const char *name, CrestRequestType type) {
   if (requests == NULL) {
      requests = (TrieTree *)malloc(sizeof(TrieTree));
      if (requests == NULL) {
         logInfo(CRITICAL, "unable to allocate memory");
         return 1;
      }
      for (int x = 0; x < 128; x++)
         requests->children[x] = NULL;
      requests->value = -1;
   }
   TrieTree *tree = requests;
   for (; *name != 0; name++) {
      if (tree->children[*name] == NULL) {
         tree->children[*name] = (TrieTree *)malloc(sizeof(TrieTree));
         if (tree->children[*name] == NULL) {
            logInfo(CRITICAL, "unable to allocate memory");
            return 1;
         }
         for (int x = 0; x < 128; x++)
            tree->children[*name]->children[x] = NULL;
         tree->children[*name]->value = -1;
      }
      tree = tree->children[*name];
   }
   tree->value = type;
   return 0;
}

char getRequest(const char *name) {
   if (requests == NULL)
      return -1;
   TrieTree *tree = requests;
   for (; *name != ' ' && *name != 0; name++) {
      if (tree->children[*name] == NULL)
         return -1;
      tree = tree->children[*name];
   }
   return tree->value;
}
void freeRequests(TrieTree *tree) {
   if (tree == NULL)
      return;
   for (int x = 0; x < 128; x++) {
      if (tree->children[x] != NULL)
         freeRequests(tree->children[x]);
   }
   free(tree);
}

void addPathVar(CrestRequest *req, unsigned index, char *name, char *value,
                int length) {
   if (index >= 3)
      return;
   if (!req || !name || !value)
      return;
   char valBuf[length + 1];
   for (int x = 0; x < length; x++)
      valBuf[x] = value[x];
   valBuf[length] = 0;
   setAdd(req->vars[index], name, valBuf);
}

int isNumeric(const char *buf) {
   if (buf == NULL)
      return 0;

   for (; *buf != 0; buf++) {
      if (*buf > '9' || *buf < '0')
         return 0;
   }
   return 1;
}

int isValidPathChar(char a) {
   if (a >= 'a' && a <= 'z')
      return 1;
   if (a >= 'A' && a <= 'Z')
      return 1;
   if (a >= '0' && a <= '9')
      return 1;
   if (a == '_' || a == '-' || a == '/')
      return 1;
   return 0;
}

CrestResponse *(*pathGetFunc2(PathTree *tree, CrestRequestType type,
                              const char *path, CrestRequest *req,
                              const char **ptr))(CrestRequest *) {
   char buf[2048];
   int x = 0;
   for (; x < 2048 && path[x] != '/' && path[x] != 0 && path[x] != ' ' &&
          path[x] != '?';
        x++) {
      if (!isValidPathChar(path[x]))
         return NULL;
      buf[x] = path[x];
   }
   buf[x] = 0;
   path += x;

   if (tree->pathVar.name == NULL)
      return NULL;

   if (tree->pathVar.type == C_DECIMAL && !isNumeric(buf))
      return NULL;

   // printf("pname: %s| buf: %s| %c\n", tree->pathVar.name, buf,*path);
   addPathVar(req, 1, tree->pathVar.name, buf, x);
   for (; *path != 0 && *path != ' ' && *path != '?'; path++) {
      if (!isValidPathChar(*path))
         return NULL;
      if (tree->var != NULL) {
         CrestResponse *(*holder)(CrestRequest *) =
             pathGetFunc2(tree->var, type, path, req, ptr);
         if (holder != NULL)
            return holder;
      }
      if (tree->children[*path] == NULL)
         return NULL;
      tree = tree->children[*path];
   }
   *ptr = path;
   return tree->func[type];
}

CrestResponse *(*pathGetFunc(CrestRequestType type, const char *path,
                             CrestRequest *req,
                             const char **ptr))(CrestRequest *) {
   if (path == NULL)
      return NULL;
   PathTree *tree = pathTree;
   for (; *path != 0 && *path != ' ' && *path != '?'; path++) {
      if (!isValidPathChar(*path))
         return NULL;
      if (tree->var != NULL) {
         CrestResponse *(*holder)(CrestRequest *) =
             pathGetFunc2(tree->var, type, path, req, ptr);
         if (holder != NULL)
            return holder;
      }
      if (tree->children[*path] == NULL)
         return NULL;
      tree = tree->children[*path];
   }
   *ptr = path;
   return tree->func[type];
}

long cookiesLen(BTreeLeaf *leaf) {
   if (leaf == NULL)
      return 0;
   return leaf->len + 20 + cookiesLen(leaf->r) + cookiesLen(leaf->l);
}

void pushCookies(BTreeLeaf *leaf, char *buf) {
   if (leaf == NULL)
      return;
   sprintf(buf, "%s\nSet-Cookie: %s=%s", buf, leaf->keyStr, leaf->value);
   pushCookies(leaf->l, buf);
   pushCookies(leaf->r, buf);
}

int sendResponse(int client, unsigned httpStatus, CrestContentType cType,
                 const char *content, Set *headers, Set *cookies) {
   time_t timer;
   time(&timer);
   struct tm *gmtTimer = gmtime(&timer);
   unsigned long contentLen = strlen(content);
   unsigned long len;
   unsigned long headersLen = setGetLen(headers) + 1;

   char headerBuf[headersLen];
   headerBuf[0] = 0;
   setPush(headers, headerBuf);

   long cookiesSize = 1;
   if (cookies != NULL)
      cookiesSize = cookiesLen(cookies->tree) + 1;

   char cookieBuf[cookiesSize];
   cookieBuf[0] = 0;
   if (cookies != NULL)
      pushCookies(cookies->tree, cookieBuf);

   char timeBuf[100];
   len = sprintf(timeBuf, "%s, %02d %s %d %02d:%02d:%02d GMT",
                 CrestWdayNames[gmtTimer->tm_wday], gmtTimer->tm_mday,
                 CrestMdayNames[gmtTimer->tm_mon], 1900 + gmtTimer->tm_year,
                 gmtTimer->tm_hour, gmtTimer->tm_min, gmtTimer->tm_sec);

   char *resBuf =
       (char *)malloc(100 + contentLen + len + headersLen + cookiesSize);
   len = sprintf(resBuf,
                 "HTTP/1.1 %d\nServer: Crest " CREST_VERSION "\nDate: %s"
                 "\nContent-Length: %lu\nContent-Type: %s%s%s\n\n%s",
                 httpStatus, timeBuf, contentLen, CrestCTNames[cType],
                 headerBuf, cookieBuf, content);
   // printf("buff size: %ld ( %ld )\n",
   //     100 + contentLen + len + headersLen + cookiesSize, strlen(resBuf));
   if (send(client, resBuf, len, 0) < 0) {
      return 1;
   }
   free(resBuf);
   close(client);
   return 0;
}
void freeRequest(CrestRequest *req) {
   if (req == NULL)
      return;
   for (int x = 0; x < 4; x++)
      setFree(req->vars[x]);
   free(req);
}
void freeResponse(CrestResponse *res) {
   setFree(res->headers);
   setFree(res->cookies);
   free(res);
}
void freePath(PathTree *tree) {
   if (tree == NULL)
      return;
   for (int x = 0; x < 128; x++)
      if (tree->children[x] != NULL)
         freePath(tree->children[x]);
   if (tree->var != NULL)
      freePath(tree->var);
   if (tree->pathVar.name != NULL)
      free(tree->pathVar.name);
   free(tree);
}
char getHexVal(char c) {
   if (c >= '0' && c <= '9')
      return c - '0';
   if (c >= 'A' && c <= 'Z')
      return c - 'A' + 10;
   return -1;
}

int setHex(char c[4]) {
   if (c[0] != '%')
      return 0;
   char holder[2] = {getHexVal(c[1]), getHexVal(c[2])};
   if (holder[0] == -1 || holder[1] == -1)
      return 0;
   c[0] = (holder[0] << 4) + holder[1];
   c[1] = 0;
   c[2] = 0;
   return 1;
}

int getQuery(CrestRequest *req, const char *path, const char **ptr) {
   if (req == NULL || path == NULL)
      return 1;

   union {
      long all;
      char c[4];
   } window;
   window.all = 0;

   char state = 0;
   char nameBuf[CREST_MAX_QUERY_NAME_LEN];
   char valBuf[CREST_MAX_QUERY_VALUE_LEN];
   unsigned long nameLen = 0;
   unsigned long valLen = 0;

   for (; *path != ' ' && *path != '\n' && *path != 0; path++) {
      window.c[3] = *path;
      if (window.c[0] != 0) {
         if (state == 0) {
            if (window.c[0] == '=') {
               if (nameLen == 0)
                  return 1;
               state = 1;
               valLen = 0;
               nameBuf[nameLen] = 0;
            } else {
               if (nameLen >= CREST_MAX_QUERY_NAME_LEN)
                  return 1;
               if (!setHex(window.c) && window.c[0] == '+')
                  nameBuf[nameLen] = ' ';
               else
                  nameBuf[nameLen] = window.c[0];
               nameLen++;
            }
         } else {
            if (window.c[0] == '&') {
               if (valLen == 0)
                  return 1;
               state = 0;
               nameLen = 0;
               valBuf[valLen] = 0;
               addPathVar(req, 2, nameBuf, valBuf, valLen);
            } else {
               if (valLen >= CREST_MAX_QUERY_VALUE_LEN)
                  return 1;
               if (!setHex(window.c) && window.c[0] == '+')
                  valBuf[valLen] = ' ';
               else
                  valBuf[valLen] = window.c[0];
               valLen++;
            }
         }
      }
      window.all >>= 8;
   }
   window.c[3] = '&';
   for (int x = 0; x < 4; x++) {
      if (window.c[0] != 0) {
         if (state == 0) {
            if (window.c[0] == '=') {
               if (nameLen == 0)
                  return 1;
               state = 1;
               valLen = 0;
               nameBuf[nameLen] = 0;
            } else {
               if (nameLen >= CREST_MAX_QUERY_NAME_LEN)
                  return 1;
               if (!setHex(window.c) && window.c[0] == '+')
                  nameBuf[nameLen] = ' ';
               else
                  nameBuf[nameLen] = window.c[0];
               nameLen++;
            }
         } else {
            if (window.c[0] == '&') {
               if (valLen == 0)
                  return 1;
               state = 0;
               nameLen = 0;
               valBuf[valLen] = 0;
               addPathVar(req, 2, nameBuf, valBuf, valLen);
            } else {
               if (valLen >= CREST_MAX_QUERY_VALUE_LEN)
                  return 1;
               if (!setHex(window.c) && window.c[0] == '+')
                  valBuf[valLen] = ' ';
               else
                  valBuf[valLen] = window.c[0];
               valLen++;
            }
         }
      }
      window.all >>= 8;
   }
   if (state)
      return 1;
   *ptr = path;
   return 0;
}
int setHeaders(CrestRequest *req, const char *ctn, const char **ptr) {
   for (; *ctn != '\n' && *ctn != 0; ctn++)
      ;
   if (*ctn == 0) // no headers found
      return 1;
   ctn++;

   char prev = 0;
   char state = 0;
   long nameLen = 0;
   long valLen = 0;
   char nameBuf[CREST_MAX_HEADER_NAME_LEN];
   char valBuf[CREST_MAX_HEADER_VALUE_LEN];

   for (;;) {
      if (*ctn == 0)
         return 1;

      if (state == 0) {
         if (*ctn == ':') {
            if (nameLen == 0)
               return 1;

            nameBuf[nameLen] = 0;
            valLen = 0;
            state = 1;
         } else {
            if (nameLen == 0 && *ctn == ' ')
               ;
            else {
               nameBuf[nameLen] = *ctn;
               nameLen++;
               if (nameLen >= CREST_MAX_HEADER_NAME_LEN)
                  return 1;
            }
         }
      } else {
         if (*ctn == '\n') {
            if (valLen == 0)
               return 1;
            valBuf[valLen - 1] = 0;
            // printf("%s: %s\n", nameBuf, valBuf);
            addPathVar(req, 0, nameBuf, valBuf, valLen);
            nameLen = 0;
            state = 0;
         } else {
            if (valLen == 0 && *ctn == ' ')
               ;
            else {
               valBuf[valLen] = *ctn;
               valLen++;
               if (valLen >= CREST_MAX_HEADER_VALUE_LEN)
                  return 1;
            }
         }
      }
      prev = *ctn;
      ctn++;
      if (*ctn == 13 && prev == '\n') {
         ctn++;
         if (*ctn == 0)
            return 1;
         ctn++;
         *ptr = ctn;
         return 0;
      }
   }
   *ptr = ctn;
   return 0;
}
void *handle(void *arg) {
   Thread *t = (Thread *)arg;
   int client = t->client;
#ifdef CREST_LOG_CONNECTIONS
   logInfo(INFO, "connection from %s", t->ip);
#endif
   char buf[CREST_INITIAL_REQUEST_LENGTH];
   ssize_t s = recv(client, buf, CREST_INITIAL_REQUEST_LENGTH, 0);
   buf[CREST_INITIAL_REQUEST_LENGTH - 1] = 0;
   char requestType = getRequest(buf);
   if (requestType == -1) {
      t->running = 0;
      return NULL;
   }

   int pathIndex = 0;
   for (pathIndex = 0; pathIndex < CREST_INITIAL_REQUEST_LENGTH; pathIndex++)
      if (buf[pathIndex] == '/')
         break;

   CrestRequest *request = (CrestRequest *)malloc(sizeof(CrestRequest));
   if (request == NULL) {
      logInfo(CRITICAL, "unable to allocate memory");
      sendResponse(client, 500, CREST_CONTENT_HTML, "internal server error",
                   NULL, NULL);
      t->running = 0;
      return NULL;
   }
   request->clientSocket = client;
   for (int x = 0; x < 3; x++) {
      request->vars[x] = setCreate();
      if (request->vars[x] == NULL) {
         sendResponse(client, 500, CREST_CONTENT_HTML, "internal server error",
                      NULL, NULL);
         t->running = 0;
         return NULL;
      }
   }
   request->vars[3] = NULL;
   request->content = NULL;
   request->requestType = (CrestRequestType)requestType;
   request->ip = t->ip;
   request->contentLen = 0;
   const char *ptr;

   CrestResponse *(*func)(CrestRequest *) = pathGetFunc(
       (CrestRequestType)requestType, buf + pathIndex, request, &ptr);

   if (func == NULL) {
      sendResponse(client, CREST_RES_NOT_FOUND, CREST_CONTENT_HTML, "not found",
                   NULL, NULL);
      freeRequest(request);
      t->running = 0;
      return NULL;
   }

   if (*ptr == '?') {
      ptr++;
      if (getQuery(request, ptr, &ptr)) {
         sendResponse(client, 400, CREST_CONTENT_HTML, "bad request", NULL,
                      NULL);
         freeRequest(request);
         t->running = 0;
         return NULL;
      }
   }
   if (setHeaders(request, ptr, &ptr)) {
      sendResponse(client, 400, CREST_CONTENT_HTML, "bad request", NULL, NULL);
      freeRequest(request);
      t->running = 0;
      return NULL;
   }

   request->contentLen = s - (ptr - buf);
   request->content = ptr;
   size_t sTotal = s;

   char *lptr = NULL;
   int offset = ptr - buf;
   const char *ctnVal = setGet(request->vars[0], cntLenHash);
   long cLen = 0;
   if (ctnVal)
      cLen = atol(ctnVal);

   if (s >= CREST_INITIAL_REQUEST_LENGTH) {
      if (cLen == 0) {
         sendResponse(client, 403, CREST_CONTENT_HTML, "invalid request", NULL,
                      NULL);
         freeRequest(request);
         t->running = 0;
         return NULL;
      }
      lptr = (char *)malloc(cLen);
      if (!lptr) {
         sendResponse(client, 500, CREST_CONTENT_HTML, "internal server error",
                      NULL, NULL);
         freeRequest(request);
         logInfo(CRITICAL, "unable to allocate memory");
         t->running = 0;
         return NULL;
      }
      offset = ptr - buf;
      for (int x = 0; x < s - offset; x++) {
         lptr[0] = (char)ptr[x];
      }
      while (sTotal < cLen) {
         size_t s2 = recv(client, lptr + sTotal, cLen - sTotal, 0);
         if (s2 < 0) {
            sendResponse(client, 500, CREST_CONTENT_HTML,
                         "internal server error", NULL, NULL);
            freeRequest(request);
            logInfo(CRITICAL, "unable to read internal buffer");
            t->running = 0;
            return NULL;
         }
         sTotal += s2;
      }
      lptr[cLen - 1] = 0;
      request->content = lptr;
      request->contentLen = sTotal - offset;
   }

   CrestResponse *response = func(request);
   if (response == NULL) {
      sendResponse(client, 500, CREST_CONTENT_HTML, "internal server error",
                   NULL, NULL);
      freeRequest(request);
      t->running = 0;
      return NULL;
   }
   sendResponse(client, response->code, response->type, response->content,
                response->headers, response->cookies);
   freeRequest(request);
   freeResponse(response);
   if (lptr)
      free(lptr);
   t->running = 0;
   return NULL;
}

void exitHandler(int sig) {
   logInfo(INFO, "exiting");
   for (int x = 0; x < CREST_MAX_THREADS; x++) {
      if (threads[x].used) {
         pthread_detach(threads[x].thr);
         logInfo(INFO, "detaching thread #%d", x + 1);
      }
   }
   freePath(pathTree);
   freeRequests(requests);
   setFreeSession(sessions);
   sem_destroy(&sessionSem);
   exit(0);
}

void crestStart(int argc, char **argv) {
#define COLORS "\033[1;96m "
   puts(COLORS
        "\033[0m\n" COLORS "    __   ______   __           _______   ________  "
        " ______   ________ \033[0m\n" COLORS
        "   /  \\ /      \\ |  \\         |       \\ |        \\ /      \\ |   "
        "     \\\033[0m\n" COLORS
        "  /  ##|  ######\\ \\##\\        | #######\\| ########|  ######\\ "
        "\\########\033[0m\n" COLORS
        " /  ## | ##   \\##  \\##\\       | ##__| ##| ##__    | ##___\\##   | "
        "##   \033[0m\n" COLORS "|  ##  | ##         >##\\      | ##    ##| ## "
        " \\    \\##    \\    | ##   \033[0m\n" COLORS
        " \\##\\  | ##   __   /  ##      | #######\\| #####    _\\######\\   | "
        "##   \033[0m\n" COLORS "  \\##\\ | ##__/  \\ /  ##       | ##  | ##| "
        "##_____ |  \\__| ##   | ##   \033[0m\n" COLORS
        "   \\##\\ \\##    ##|  ##        | ##  | ##| ##     \\ \\##    ##   | "
        "##   \033[0m\n" COLORS
        "    \\##  \\######  \\##          \\##   \\## \\########  \\######    "
        " \\##   \033[0m\n" COLORS);
#ifdef CREST_RANDOM_SLOGAN
   printf("   %s", CrestSlogans[arc4random_uniform((sizeof(CrestSlogans)) /
                                                   (sizeof(char) * 27))]);
   puts("                              (ver. " CREST_VERSION ")\033[0m\n");
#endif
#ifndef CREST_RANDOM_SLOGAN
   puts(COLORS "                                                         "
               "(ver. " CREST_VERSION ")\033[0m\n");
#endif
   int serverSoc = -1;
   struct sockaddr_in addr;
   logInfo(INFO, "creating socket");
   serverSoc = socket(AF_INET, SOCK_STREAM, 0);
   if (serverSoc < 0) {
      logInfo(CRITICAL, "unable to create socket; exiting");
      return;
   }

#ifdef CREST_REUSE_SOCKET
   int holder = 1;
   setsockopt(serverSoc, SOL_SOCKET, SO_REUSEADDR, &holder, sizeof(int));
#endif

   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(CREST_PORT);
   addr.sin_family = AF_INET;

   logInfo(INFO, "binding socket");
   if (bind(serverSoc, (struct sockaddr *)&addr, sizeof(addr))) {
      logInfo(CRITICAL, "unable to bind socket; exiting");
      return;
   }

   logInfo(INFO, "starting listener");
   if (listen(serverSoc, CREST_MAX_CONNECTIONS)) {
      logInfo(CRITICAL, "unable start listener; exiting");
      return;
   }
   sem_init(&sessionSem, 0, 1);

   int retVal = 0;

   /*--WALL-OF-SHAME--*/
   retVal |= addRequest("GET", CREST_GET);
   retVal |= addRequest("HEAD", CREST_HEAD);
   retVal |= addRequest("PUT", CREST_PUT);
   retVal |= addRequest("POST", CREST_POST);
   retVal |= addRequest("DELETE", CREST_DELETE);
   retVal |= addRequest("OPTIONS", CREST_OPTIONS);
   retVal |= addRequest("TRACE", CREST_TRACE);
   retVal |= addRequest("CONNECT", CREST_CONNECT);
   retVal |= addRequest("PATCH", CREST_PATCH);
   /*-----------------*/

   if (retVal) {
      logInfo(CRITICAL,
              "Due to memory allocation problems unable to start; exiting");
      return;
   }

   sessions = setCreate();
   if (sessions == NULL) {
      logInfo(CRITICAL,
              "Due to memory allocation problems unable to start; exiting");
      return;
   }

   signal(SIGKILL, exitHandler);
   signal(SIGINT, exitHandler);
   signal(SIGTERM, exitHandler);

   cntLenHash = murmur3_32("Content-Length", strlen("Content-Length"));
   setCookieHash = murmur3_32("Set-Cookie", strlen("Set-Cookie"));
   cookieHash = murmur3_32("Cookie", strlen("Cookie"));

   socklen_t sockLen = 16;
   struct sockaddr_in cliAddr;
   cliAddr.sin_family = INADDR_ANY;
   cliAddr.sin_addr.s_addr = 0;
   cliAddr.sin_port = 0;
   cliAddr.sin_family = AF_INET;

   for (int x = 0; x < CREST_MAX_THREADS; x++) {
      threads[x].used = 0;
      threads[x].running = 0;
      threads[x].client = 0;
      threads[x].id = x;
      threads[x].thr = NULL;
   }
   char unhandled = 0;

   for (;;) {
      int client = accept(serverSoc, (struct sockaddr *)&cliAddr, &sockLen);
      unhandled = 1;
      while (unhandled) {
         for (int x = 0; x < CREST_MAX_THREADS; x++) {
            if (threads[x].running == 0) {
               threads[x].client = client;
               threads[x].ip = inet_ntoa(cliAddr.sin_addr);
               if (threads[x].used == 0)
                  threads[x].used = 1;
               else
                  pthread_join(threads[x].thr, NULL);
               threads[x].running = 1;
               pthread_create(&threads[x].thr, NULL, handle,
                              (void *)&(threads[x]));
               unhandled = 0;
               break;
            }
         }

#ifdef CREST_THREAD_OVERLOAD_LOG
         if (unhandled == 1) {
            logInfo(MINOR, "all threads are busy");
            unhandled = 2;
         }
#endif
      }
   }
   freePath(pathTree);
   freeRequests(requests);
   setFreeSession(sessions);
   sem_destroy(&sessionSem);
}

int crestAddHandler(CrestResponse *(*func)(CrestRequest *),
                    CrestRequestType type, const char *path) {
   if (path == NULL || func == NULL)
      return 1;
   if (*path != '/') {
      logInfo(MINOR, "invalid path \"%s\" handler will not be used", path);
      return 1;
   }
   const char *pathcpy = path;

   if (pathTree == NULL) {
      pathTree = (PathTree *)malloc(sizeof(PathTree));
      if (pathTree == NULL) {
         logInfo(CRITICAL, "unable to allocate memory");
         return 1;
      }
      for (int x = 0; x < 9; x++)
         pathTree->func[x] = NULL;
      for (int x = 0; x < 128; x++)
         pathTree->children[x] = NULL;
      pathTree->pathVar.type = C_NONE;
      pathTree->pathVar.name = NULL;
      pathTree->var = NULL;
   }
   PathTree *tree = pathTree;
   char prev = 0;

   for (; *path != 0; path++) {
      // path varable
      if (prev == '/' && *path == '%') {
         tree->var = (PathTree *)malloc(sizeof(PathTree));
         if (tree->var == NULL) {
            logInfo(CRITICAL, "unable to allocate memory");
            return 1;
         }
         for (int x = 0; x < 9; x++)
            tree->var->func[x] = NULL;

         for (int x = 0; x < 128; x++)
            tree->var->children[x] = NULL;

         tree->var->pathVar.type = C_NONE;
         tree->var->pathVar.name = NULL;
         tree->var->var = NULL;
         tree = tree->var;
         path++;
         if (*path == 's')
            tree->pathVar.type = C_STRING;
         else if (*path == 'd')
            tree->pathVar.type = C_DECIMAL;
         else
            return 1;
         path++;
         if (*path != '<')
            return 1;
         path++;
         int varLen = 0;

         for (; *path != '>' && *path != 0; path++)
            varLen++;

         tree->pathVar.name = (char *)malloc(sizeof(char) * (varLen + 1));
         if (tree->pathVar.name == NULL) {
            logInfo(CRITICAL, "unable to allocate memory");
            return 1;
         }
         path -= varLen;
         for (int x = 0; x < varLen; x++) {
            tree->pathVar.name[x] = *path;
            path++;
         }
         tree->pathVar.name[varLen] = 0;

         continue;
      }
      // normal path
      if (tree->children[*path] == NULL) {
         tree->children[*path] = (PathTree *)malloc(sizeof(PathTree));
         if (tree->children[*path] == NULL) {
            logInfo(CRITICAL, "unable to allocate memory");
            return 1;
         }

         for (int x = 0; x < 128; x++)
            tree->children[*path]->children[x] = NULL;

         for (int x = 0; x < 9; x++)
            tree->children[*path]->func[x] = NULL;

         tree->children[*path]->pathVar.type = C_NONE;
         tree->children[*path]->pathVar.name = NULL;
         tree->children[*path]->var = NULL;
      }
      prev = *path;
      tree = tree->children[*path];
   }
   tree->func[type] = func;

   return 0;
}

CrestResponse *crestGenResponseF(unsigned code, const char *content,
                                 int flags) {
   CrestResponse *res = (CrestResponse *)malloc(sizeof(CrestResponse));
   if (res == NULL) {
      logInfo(CRITICAL, "unable to allocate memory");
      return NULL;
   }
   res->code = code;
   res->content = content;
   res->flags = flags;
   res->headers = setCreate();
   if (!res->headers) {
      logInfo(CRITICAL, "unable to allocate memory");
      return NULL;
   }
   res->cookies = setCreate();
   if (!res->cookies) {
      logInfo(CRITICAL, "unable to allocate memory");
      return NULL;
   }
   if (content == NULL) {
      res->content = "";
      res->type = CREST_CONTENT_HTML;
   } else if (content[0] == '[' || content[0] == '{')
      res->type = CREST_CONTENT_JSON;
   else
      res->type = CREST_CONTENT_HTML;

   return res;
}

CrestResponse *crestGenResponse(unsigned code, const char *content) {
   return crestGenResponseF(code, content, 0);
}

/* -- getters for path vars, querys and headers -- */

const char *crestGetVar(CrestRequest *req, char *name) {
   // TODO: fix decimal/string handling
   if (name == NULL || req == NULL)
      return "";
   const char *ptr = setGetByName(req->vars[1], name);
   if (!ptr)
      return "";
   return ptr;
}

const char *crestGetQuery(CrestRequest *req, char *name) {
   if (name == NULL || req == NULL)
      return "";
   const char *ptr = setGetByName(req->vars[2], name);
   if (!ptr)
      return "";
   return ptr;
}

const char *crestGetHeader(CrestRequest *req, char *name) {
   if (name == NULL || req == NULL)
      return "";
   const char *ptr = setGetByName(req->vars[0], name);
   if (!ptr)
      return "";
   return ptr;
}

const char *crestGetVarPtr(CrestRequest *req, char *name) {
   // TODO: fix decimal/string handling
   if (name == NULL || req == NULL)
      return NULL;
   return setGetByName(req->vars[1], name);
}

const char *crestGetQueryPtr(CrestRequest *req, char *name) {
   if (name == NULL || req == NULL)
      return NULL;
   return setGetByName(req->vars[2], name);
}

const char *crestGetHeaderPtr(CrestRequest *req, char *name) {
   if (name == NULL || req == NULL)
      return NULL;
   return setGetByName(req->vars[0], name);
}

int crestSetHeader(CrestResponse *res, char *name, char *value) {
   if (name == NULL || res == NULL || value == NULL) {
      logInfo(MINOR, "unable to set header \"%s\" value", name);
      return -1;
   }
   setAdd(res->headers, name, value);
   return 0;
}

int crestSetCookie(CrestResponse *res, char *name, char *value) {
   if (name == NULL || res == NULL || value == NULL) {
      logInfo(MINOR, "unable to set cookie \"%s\" value", name);
      return -1;
   }
   setAdd(res->cookies, name, value);
   return 0;
}

int crestDropCookie(CrestResponse *res, char *name) {
   if (name == NULL || res == NULL) {
      logInfo(MINOR, "unable to drop cookie\"%s\" value", name);
      return -1;
   }
   setAdd(res->cookies, name, "");
   return 0;
}

const char *crestGetCookiePtr(CrestRequest *req, char *name) {
   if (req->vars[3] == NULL) {
      req->vars[3] = setCreate();
      if (req->vars[3] == NULL) {
         logInfo(MINOR, "unable to get cookie\"%s\" value", name);
         return NULL;
      }
      BTreeLeaf *leaf = setGetLeaf(req->vars[0], cookieHash);
      if (leaf == NULL)
         return NULL;

      const char *val = leaf->value;
      if (val == NULL)
         return NULL;

      char nameBuf[CREST_MAX_COOKIE_NAME_LEN];
      char valBuf[CREST_MAX_COOKIE_VALUE_LEN];
      int ctn;
      int nameLen = 0;
      int valLen = 0;
      char holder;
      char state = 0;

      for (ctn = 0;; ctn++) {
         holder = val[ctn];
         if (!holder)
            break;
         if (!state) { // name
            if (holder == ' ')
               continue;
            if (holder == '=') {
               state = 1;
               valLen = 0;
               continue;
            }
            nameBuf[nameLen] = holder;
            nameLen++;
            if (nameLen >= CREST_MAX_COOKIE_NAME_LEN) {
               logInfo(MINOR,
                       "unable to get cookie value due to bad request body",
                       name);
               return NULL;
            }
         } else { // value
            if (holder == ';') {
               if (nameLen == 0) {
                  logInfo(MINOR,
                          "unable to get cookie value due to bad request body",
                          name);
                  return NULL;
               }
               nameBuf[nameLen] = 0;
               valBuf[valLen] = 0;
               setAdd(req->vars[3], nameBuf, valBuf);
               // printf("\"%s\" = \"%s\"\n", nameBuf, valBuf);
               state = 0;
               nameLen = 0;
               continue;
            }
            valBuf[valLen] = holder;
            valLen++;
            if (valLen >= CREST_MAX_COOKIE_VALUE_LEN) {
               logInfo(MINOR,
                       "unable to get cookie value due to bad request body",
                       name);
               return NULL;
            }
         }
      }
      if (valLen != 0) {
         if (nameLen == 0) {
            logInfo(MINOR, "unable to get cookie value due to bad request body",
                    name);
            return NULL;
         }
         nameBuf[nameLen] = 0;
         valBuf[valLen] = 0;
         // printf("%s = %s\n", nameBuf, valBuf);
         setAdd(req->vars[3], nameBuf, valBuf);
      }
   }
   return setGetByName(req->vars[3], name);
}

const char *crestGetCookie(CrestRequest *req, char *name) {
   const char *ret = crestGetCookiePtr(req, name);
   return ret ? ret : "";
}

void *crestGetSession(CrestRequest *req) {
   unsigned sessionId = atol(crestGetCookie(req, "session"));
   if (!sessionId)
      return NULL;
   void *obj = (void *)setGet(sessions, sessionId);
   return obj;
}

int crestSetSession(CrestResponse *res, void *sessionObj) {
   unsigned int sessionId;
   arc4random_buf(&sessionId, sizeof(sessionId));
   char valBuf[20];

   sem_wait(&sessionSem);
   setAddSession(sessions, sessionId, sessionObj);
   sem_post(&sessionSem);

   sprintf(valBuf, "%u", sessionId);
   sessionId = setAdd(res->cookies, "session", valBuf);

   return sessionId;
}

int crestDropSession(CrestRequest *req) {
   logInfo(MINOR, "crestDropSession is not implemented");
   return 1;
}

int crestSetSessionDropFunc(int (*func)(void *)) {
#ifdef CREST_WARN_ON_REUSE
   static char used = 0;
   if (used)
      logInfo(MINOR, "crestSessionDiscardFunc should be called only once");
   else
      used = 1;
#endif
   sessionFreeFunc = func;
}
/* -- -- */
