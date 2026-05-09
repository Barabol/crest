# crest

### (maybe) fast REST API framework
 
## usage

**basics**
```c
/**
* Crest endpoints must return CrestResponse ptr
*
* and must take CrestRequest ptr as argument
*/
CrestResponse *getIp(CrestRequest *req) {
   CrestResponse *res = crestGenResponse(200, req->ip);

   // content type is automaticly detected but you can set it manualy
   res->type = CREST_CONTENT_HTML;

   // if endpoint returns NULL client will get "internal server error"
   return res; 
}

int main(int argc, char **argv) {
   // endpoints must be defined before starting crest
   crestAddHandler(getIp, CREST_GET, "/ip");
   crestStart(argc, argv);
}

```

**Path Variables**

```c
CrestResponse *returnPathVar(CrestRequest *req) {
   CrestResponse *res = crestGenResponse(200, crestGetVar(req, "test"));
   res->type = CREST_CONTENT_HTML;
   return res;
}

int main(int argc, char **argv) {
   // if you want to define path variable you define it with expected type
   // %s for string 
   // %d for decimal
   // and after that you define name of path var inside '<' '>' symbols 
   crestAddHandler(returnPathVar, CREST_GET, "/test/%s<test>");
   crestStart(argc, argv);
}

```
**Query Variables**

```c
CrestResponse *returnQuery(CrestRequest *req) {
   CrestResponse *res = crestGenResponse(200, crestGetQuery(req, "test"));
   res->type = CREST_CONTENT_HTML;
   return res;
}

int main(int argc, char **argv) {
   crestAddHandler(returnQuery, CREST_GET, "/test");
   crestStart(argc, argv);
}
```
**Adding response headers**
```c
CrestResponse *getIp(CrestRequest *req) {
   CrestResponse *res = crestGenResponse(200, req->ip);
   res->type = CREST_CONTENT_HTML;
   crestSetHeader(res, "Custom-Header", "test value");
   return res; 
}

int main(int argc, char **argv) {
   crestAddHandler(getIp, CREST_GET, "/ip");
   crestStart(argc, argv);
}

```
**Setting and receiving cookies**
```c
CrestResponse *cookie(CrestRequest *req) {
   CrestResponse *res = crestGenResponse(200, "");
   // sets client side cookie
   crestSetCookie(res, "test", "test a");

   // getting "test" cookie if doesn't exist function will return NULL
   if(crestGetCookiePtr(req,"test") == NULL)
       // deletes client side cookie
       crestDropCookie(res, "test");
   
   return res; 
}

int main(int argc, char **argv) {
   crestAddHandler(cookie, CREST_GET, "/cookie");
   crestStart(argc, argv);
}

```

**Session**

```c
typedef struct {
   int counter;
} SessionObj;

int freeSessionObj(void *obj) {
   if (obj)
      free(obj);
   return 0;
}

CrestResponse *sessionTest(CrestRequest *req) {

   // if you want to allocate response body on heap you should set this flag
   CrestResponse *res = crestGenResponseF(200, "", CREST_RES_F_FREE_RES_BODY);

   // getting session object from session
   SessionObj *session = (SessionObj *)crestGetSession(req);

   char *buff = (char *)malloc(sizeof(char) * 1024);

   if (session) {// just counter incrementation
      sprintf(buff, "counter: %u", session->counter);
      session->counter++;
   } else {// if session object does not exist (so session does not exist) create one and send session id in html
      session = (SessionObj *)malloc(sizeof(SessionObj));
      session->counter = 0;
      sprintf(buff, "session id: %u", crestSetSession(res, (void *)session));
   }

   res->content = buff;
   return res;
}

int main(int argc, char **argv) {
   // drop Function should be set if object is allocated and has no other way of being freed
   crestSetSessionDropFunc(freeSessionObj);
   crestAddHandler(sessionTest, CREST_GET, "/test");
   crestStart(argc, argv);
}
```

There is easier way of creating/receiving sessions if you don't need session id

```c
typedef struct {
   int counter;
} SessionObj;

int freeSessionObj(void *obj) {
   if (obj)
      free(obj);
   return 0;
}

// function or macro that sets values for new session object
void SessionObjSet(SessionObj *obj) { obj -> counter = 0; };

CrestResponse *sessionTestMacro(CrestRequest *req) {
   CrestResponse *res = crestGenResponseF(200, "", CREST_RES_F_FREE_RES_BODY);

   // if you don't need to know if session is just created you can use crestSession macro
   SessionObj *session = crestSession(SessionObj, req, res, SessionObjSet);

   char *buff = (char *)malloc(sizeof(char) * 1024);
   sprintf(buff, "counter: %u", session->counter);
   session->counter++;

   res->content = buff;
   return res;
}

int main(int argc, char **argv) {
   crestSetSessionDropFunc(freeSessionObj);
   crestAddHandler(sessionTestMacro, CREST_GET, "/test");
   crestStart(argc, argv);
}
```
