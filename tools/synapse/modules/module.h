#pragma once

#include "call-paths-to-bdd.h"
#include "../execution_plan/visitors/visitor.h"

#define SHARED_THIS_MODULE (std::shared_ptr<__Module>(this))
#define MODULE(X)          (std::make_shared<X>())

namespace synapse {

enum Target {
  x86, Tofino, Netronome, FPGA
};

class   ExecutionPlan;
class    __Module;

typedef std::shared_ptr<__Module>  Module;

class Context {
private:
  std::vector<ExecutionPlan> next_eps;
  ExecutionPlan*             current_ep;
  bool                       success;
 
public:
  // context_t(ExecutionPlan ep) : current_ep(ep), success(false) {}
  Context() : success(false) {}
  Context(const BDD::Node* node) {
    next_eps.emplace_back(node);
  }

  void add(ExecutionPlan& next_ep) {
    next_eps.push_back(next_ep);
    success = true;
  }

  void reset(ExecutionPlan* _current_ep) {
    next_eps.clear();
    current_ep = _current_ep;
    success = false;
  }

  const ExecutionPlan& get_current() const {
    return *current_ep;
  }

  const std::vector<ExecutionPlan>& get_next_eps() const {
    return next_eps;
  }

  bool processed() const { return success; }
  int  size()      const { return next_eps.size(); }

  void set_processed(bool _success) {
    success = _success;
  }
};

class __Module : public BDD::BDDVisitor {
protected:
  Target      target;
  const char* name;
  BDD::Node*  node;

  Context     context; // intermediary data

protected:
  __Module(Target _target, const char* _name)
    : target(_target), name(_name) {}

public:
  __Module() {}
  __Module(const __Module& m) : node(m.node) {}

  const char* get_name()   const { return name;   }
  Target      get_target() const { return target; }
  BDD::Node*  get_node()   const { return node;   }

  std::string get_target_name() {
    switch (target) {
      case x86:
        return "x86";
      case Tofino:
        return "Tofino";
      case Netronome:
        return "Netronome";
      case FPGA:
        return "FPGA";
    }

    assert(false && "I should not be here");
  }

  Context& process_node(ExecutionPlan _ep, const BDD::Node* node);

  virtual void visit(ExecutionPlanVisitor& visitor) const = 0;
};

}
