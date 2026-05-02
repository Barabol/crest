# crest
 
### usage

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
