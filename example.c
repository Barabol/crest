#include "./crest.h"
#include <stdio.h>

CrestResponse *returnQuery(CrestRequest *req) {
   CrestResponse *res = crestGenResponse(200, crestGetQuery(req, "test"));
	crestSetHeader(res, "test", "test");
	crestSetHeader(res, "test1", "test1");
	crestSetHeader(res, "test2", "test");
	crestSetHeader(res, "test3", "test1");
	crestSetHeader(res, "tes4", "test");
	crestSetHeader(res, "test5", "test1");
   res->type = CREST_CONTENT_HTML;
   return res;
}

int main(int argc, char **argv) {
   crestAddHandler(returnQuery, CREST_GET, "/test");
   crestStart(argc, argv);
}
