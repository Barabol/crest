#include "./crest.h"

CrestResponse *getIp(CrestRequest *req) {
	CrestResponse *res = crestGenResponse(200, req->ip);
	res->type = CREST_CONTENT_HTML;
   return res;
}

int main(int argc, char **argv) {
   crestAddHandler(getIp, CREST_GET, "/ip");
   crestStart(argc, argv);
}
