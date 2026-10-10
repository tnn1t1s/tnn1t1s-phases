#include <rack.hpp>

extern rack::Model* modelPhase;
extern rack::Model* modelExpr;

rack::Plugin* pluginInstance;


void init(rack::Plugin* p) {
    pluginInstance = p;
    p->addModel(modelPhase);
    p->addModel(modelExpr);
}
