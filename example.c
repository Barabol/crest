#include "./crest.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
   int counter;
} SessionObj;

int freeSessionObj(void *obj) {
   if (obj)
      free(obj);
   return 0;
}

void SessionObjSet(SessionObj *obj) { obj -> counter = 0; };

CrestResponse *sessionTestMacro(CrestRequest *req) {
   CrestResponse *res = crestGenResponseF(200, "", CREST_RES_F_FREE_RES_BODY);

   SessionObj *session = crestSession(SessionObj, req, res, SessionObjSet);

   char *buff = (char *)malloc(sizeof(char) * 1024);
   sprintf(buff, "counter: %u", session->counter);
   session->counter++;

   res->content = buff;
   return res;
}

CrestResponse *sessionTest(CrestRequest *req) {

   CrestResponse *res = crestGenResponseF(200, "", CREST_RES_F_FREE_RES_BODY);

   SessionObj *session = (SessionObj *)crestGetSession(req);

   char *buff = (char *)malloc(sizeof(char) * 1024);

   if (session) {
      session->counter++;
   } else {
      session = (SessionObj *)malloc(sizeof(SessionObj));
      session->counter = 0;
   }
   sprintf(buff, "counter: %u", session->counter);
   res->content = buff;
   return res;
}

CrestResponse *file(CrestRequest *req) {
   FILE *file = fopen("test.mp3", "w+");
   for (int x = 0; x < req->contentLen; x++) {
      fprintf(file, "%c", req->content[x]);
   }
   printf("%s\n", crestGetCookiePtr(req, "test"));
   fclose(file);

   CrestResponse *res = crestGenResponse(200, "OK");

   return res;
}

int main(int argc, char **argv) {
   crestSetSessionDropFunc(freeSessionObj);
   crestAddHandler(sessionTest, CREST_GET, "/test");
   crestAddHandler(sessionTestMacro, CREST_GET, "/macro");
   crestAddHandler(file, CREST_POST, "/file");
   crestStart(argc, argv);
}
