#include "crest.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
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

void addRequest(const char *name, CrestRequestType type) {
   if (requests == NULL) {
      requests = (TrieTree *)malloc(sizeof(TrieTree));
      if (requests == NULL) {
         logInfo(CRITICAL, "unable to allocate memory");
         return;
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
            return;
         }
         for (int x = 0; x < 128; x++)
            tree->children[*name]->children[x] = NULL;
         tree->children[*name]->value = -1;
      }
      tree = tree->children[*name];
   }
   tree->value = type;
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

void addPathVar(CrestRequest *req, unsigned index, const char *name,
                char *value, int length) {
   if (index >= 3)
      return;
   if (req->vars == NULL) {
      req->vars = (CrestTree *)malloc(sizeof(CrestTree));
      if (req->vars == NULL) {
         logInfo(CRITICAL, "unable to allocate memory");
         return;
      }
      for (int x = 0; x < 3; x++)
         req->vars->value[x] = NULL;
      for (int x = 0; x < 128; x++)
         req->vars->children[x] = NULL;
   }

   CrestTree *tree = req->vars;
   for (; *name != 0; name++) {
      if (tree->children[*name] == NULL) {
         tree->children[*name] = (CrestTree *)malloc(sizeof(CrestTree));
         if (tree->children[*name] == NULL) {
            logInfo(CRITICAL, "unable to allocate memory");
            return;
         }
         for (int x = 0; x < 3; x++)
            tree->children[*name]->value[x] = NULL;
         for (int x = 0; x < 128; x++)
            tree->children[*name]->children[x] = NULL;
      }
      tree = tree->children[*name];
   }

   if (tree->value[index] != NULL)
      free(tree->value[index]);

   tree->value[index] = (char *)malloc(sizeof(char) * (length + 1));
   if (tree->value[index] == NULL) {
      logInfo(CRITICAL, "unable to allocate memory");
      return;
   }
   sprintf(tree->value[index], "%s", value);
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

int sendResponse(int client, unsigned httpStatus, CrestContentType cType,
                 const char *content) {
   time_t timer;
   time(&timer);
   struct tm *gmtTimer = gmtime(&timer);
   unsigned long contentLen = strlen(content);
   unsigned long len;

   char timeBuf[100];
   len = sprintf(timeBuf, "%s, %02d %s %d %02d:%02d:%02d GMT",
                 CrestWdayNames[gmtTimer->tm_wday], gmtTimer->tm_mday,
                 CrestMdayNames[gmtTimer->tm_mon], 1900 + gmtTimer->tm_year,
                 gmtTimer->tm_hour, gmtTimer->tm_min, gmtTimer->tm_sec);

   char resBuf[100 + contentLen + len];
   len = sprintf(resBuf,
                 "HTTP/1.1 %d\nServer: Crest " CREST_VERSION "\nDate: %s"
                 "\nContent-Length: %lu\nContent-Type: %s\n\n%s",
                 httpStatus, timeBuf, contentLen, CrestCTNames[cType], content);

   if (send(client, resBuf, len, 0) < 0) {
      return 1;
   }
   close(client);
   return 0;
}
void freeCrestTree(CrestTree *tree) {
   if (tree == NULL)
      return;
   for (int x = 0; x < 128; x++)
      if (tree->children[x] != NULL)
         freeCrestTree(tree->children[x]);
   for (int x = 0; x < 3; x++)
      if (tree->value[x] != NULL)
         free(tree->value[x]);
   free(tree);
}
void freeRequest(CrestRequest *req) {
   if (req == NULL)
      return;
   freeCrestTree(req->vars);
   free(req);
}
void freeResponse(CrestResponse *res) { free(res); }
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
   char buf[CREST_MAX_REQUEST_LENGTH];
   recv(client, buf, CREST_MAX_REQUEST_LENGTH, 0);
   buf[CREST_MAX_REQUEST_LENGTH - 1] = 0;
   char requestType = getRequest(buf);
   if (requestType == -1) {
      t->running = 0;
      return NULL;
   }

   int pathIndex = 0;
   for (pathIndex = 0; pathIndex < CREST_MAX_REQUEST_LENGTH; pathIndex++)
      if (buf[pathIndex] == '/')
         break;

   CrestRequest *request = (CrestRequest *)malloc(sizeof(CrestRequest));
   if (request == NULL) {
      logInfo(CRITICAL, "unable to allocate memory");
      sendResponse(client, 500, CREST_CONTENT_HTML, "internal server error");
      t->running = 0;
      return NULL;
   }
   request->clientSocket = client;
   request->vars = NULL;
   request->content = NULL;
   request->requestType = CREST_GET;
   request->ip = t->ip;
   const char *ptr;

   CrestResponse *(*func)(CrestRequest *) = pathGetFunc(
       (CrestRequestType)requestType, buf + pathIndex, request, &ptr);

   if (func == NULL) {
      sendResponse(client, CREST_RES_NOT_FOUND, CREST_CONTENT_HTML,
                   "not found");
      freeRequest(request);
      t->running = 0;
      return NULL;
   }

   if (*ptr == '?') {
      ptr++;
      if (getQuery(request, ptr, &ptr)) {
         sendResponse(client, 400, CREST_CONTENT_HTML, "bad request");
         freeRequest(request);
         t->running = 0;
         return NULL;
      }
   }
   if (setHeaders(request, ptr, &ptr)) {
      sendResponse(client, 400, CREST_CONTENT_HTML, "bad request");
      freeRequest(request);
      t->running = 0;
      return NULL;
   }
   request->content = ptr;

   CrestResponse *response = func(request);
   if (response == NULL) {
      sendResponse(client, 500, CREST_CONTENT_HTML, "internal server error");
      freeRequest(request);
      t->running = 0;
      return NULL;
   }
   sendResponse(client, response->code, response->type, response->content);
   freeRequest(request);
   freeResponse(response);
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
   srand(time(NULL));

   printf(
       "   %s",
       CrestSlogans[random() % ((sizeof(CrestSlogans)) / (sizeof(char) * 27))]);
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

   /*--WALL-OF-SHAME--*/
   addRequest("GET", CREST_GET);
   addRequest("HEAD", CREST_HEAD);
   addRequest("PUT", CREST_PUT);
   addRequest("POST", CREST_POST);
   addRequest("DELETE", CREST_DELETE);
   addRequest("OPTIONS", CREST_OPTIONS);
   addRequest("TRACE", CREST_TRACE);
   addRequest("CONNECT", CREST_CONNECT);
   addRequest("PATCH", CREST_PATCH);
   /*-----------------*/

   signal(SIGKILL | SIGTERM | SIGINT, exitHandler);

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
      threads[x].thr = -1;
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
         if (unhandled == 1) {
            logInfo(MINOR, "all threads are busy");
            unhandled = 2;
         }
      }
   }
   freePath(pathTree);
   freeRequests(requests);
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

CrestResponse *crestGenResponse(unsigned code, const char *content) {
   CrestResponse *res = (CrestResponse *)malloc(sizeof(CrestResponse));
   if (res == NULL) {
      logInfo(CRITICAL, "unable to allocate memory");
      return NULL;
   }
   res->code = code;
   res->content = content;
   if (content == NULL) {
      res->content = "";
      res->type = CREST_CONTENT_HTML;
   } else if (content[0] == '[' || content[0] == '{')
      res->type = CREST_CONTENT_JSON;
   else
      res->type = CREST_CONTENT_HTML;

   return res;
}

/* -- getters for path vars, querys and headers -- */

const char *crestGetVar(CrestRequest *req, const char *name) {
   if (name == NULL || req == NULL || req->vars == NULL)
      return "";
   CrestTree *tree = req->vars;
   for (; *name != 0; name++) {
      if (tree->children[*name] == NULL)
         return "";
      tree = tree->children[*name];
   }
   if (tree->value[1] == NULL)
      return "";
   return tree->value[1];
}
const char *crestGetQuery(CrestRequest *req, const char *name) {
   if (name == NULL || req == NULL || req->vars == NULL)
      return "";
   CrestTree *tree = req->vars;
   for (; *name != 0; name++) {
      if (tree->children[*name] == NULL)
         return "";
      tree = tree->children[*name];
   }
   if (tree->value[2] == NULL)
      return "";
   return tree->value[2];
}
const char *crestGetHeader(CrestRequest *req, const char *name) {
   if (name == NULL || req == NULL || req->vars == NULL)
      return "";
   CrestTree *tree = req->vars;
   for (; *name != 0; name++) {
      if (tree->children[*name] == NULL)
         return "";
      tree = tree->children[*name];
   }
   if (tree->value[0] == NULL)
      return "";
   return tree->value[0];
}
const char *crestGetVarPtr(CrestRequest *req, const char *name) {
   if (name == NULL || req == NULL || req->vars == NULL)
      return NULL;
   CrestTree *tree = req->vars;
   for (; *name != 0; name++) {
      if (tree->children[*name] == NULL)
         return NULL;
      tree = tree->children[*name];
   }
   if (tree->value[1] == NULL)
      return NULL;
   return tree->value[1];
}
const char *crestGetQueryPtr(CrestRequest *req, const char *name) {
   if (name == NULL || req == NULL || req->vars == NULL)
      return NULL;
   CrestTree *tree = req->vars;
   for (; *name != 0; name++) {
      if (tree->children[*name] == NULL)
         return NULL;
      tree = tree->children[*name];
   }
   if (tree->value[2] == NULL)
      return NULL;
   return tree->value[2];
}
const char *crestGetHeaderPtr(CrestRequest *req, const char *name) {
   if (name == NULL || req == NULL || req->vars == NULL)
      return NULL;
   CrestTree *tree = req->vars;
   for (; *name != 0; name++) {
      if (tree->children[*name] == NULL)
         return NULL;
      tree = tree->children[*name];
   }
   if (tree->value[0] == NULL)
      return NULL;
   return tree->value[0];
}

/* -- -- */
