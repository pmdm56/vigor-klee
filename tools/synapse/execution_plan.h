#pragma once

#include "call-paths-to-bdd.h"
#include "modules/module.h"

namespace synapse {

class Module;

class ExecutionPlanNode {
private:
  Module* module;
  BDD::Node* node;
};

class ExecutionPlan {
private:
  ExecutionPlanNode* root;
};

}