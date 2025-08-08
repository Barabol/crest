#include "./crest.h"
#include <stdio.h>

CrestResponse *test(CrestRequest *req) {
   printf("id: %s, test: %s\n", crestGetVar(req, "id"),
          crestGetVar(req, "test"));
   return crestGenResponse(200, crestGetVar(req, "id"));
}

CrestResponse *getIp(CrestRequest *req) {
   return crestGenResponse(200, req->ip);
}

CrestResponse *test2(CrestRequest *req) {
   const char *header = crestGetHeader(req, "Authorization");
   return crestGenResponse(200, crestGetQuery(req, "a"));
}

int main(int argc, char **argv) {
   crestAddHandler(test, CREST_GET, "/test/%d<id>/%s<test>");
   crestAddHandler(test2, CREST_GET, "/test");
   crestAddHandler(test2, CREST_GET, "/test");
   crestAddHandler(getIp, CREST_GET, "/ip");
   crestStart(argc, argv);
}
