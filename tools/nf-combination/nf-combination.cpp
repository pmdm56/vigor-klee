#include "llvm/Support/CommandLine.h"
#include "bdd-conflict-detect.h"

#include <list>

enum WritePolicyOpt {
  w1, w2
};

enum ForwardPolicyOpt {
  any_drop, both_drop, bdd1_drop, bdd2_drop
};

enum ForwardingDevPolicyOpt {
 dev1, dev2
};

namespace {

//input BDD files & output BDD
llvm::cl::opt<std::string> BDD1("bdd1", llvm::cl::Required, 
  llvm::cl::desc("First bdd"), llvm::cl::value_desc("<file>.bdd"));
llvm::cl::opt<std::string> BDD2("bdd2", llvm::cl::Required, 
  llvm::cl::desc("Second bdd"), llvm::cl::value_desc("<file>.bdd"));

//handle write conflict
llvm::cl::opt<WritePolicyOpt> WritePolicy("prior_changes", 
llvm::cl::Required, llvm::cl::desc("Write conflict resolution:"),
  llvm::cl::values(
    clEnumVal(w1 , "Prioritize bdd1 packet writes"),
    clEnumVal(w2 , "Prioritize bdd2 packet writes"),
    clEnumValEnd));

//handle forwarding conflict
llvm::cl::opt<ForwardPolicyOpt> ForwardPolicy("drop_when", 
llvm::cl::Required, llvm::cl::desc("Forward conflict resolution:"),
  llvm::cl::values(
    clEnumVal(any_drop, "Drop packet if at least one drops"),
    clEnumVal(both_drop, "Drop packet if only both drop"),
    clEnumVal(bdd1_drop, "Drop packet if bdd1 drops"),
    clEnumVal(bdd2_drop, "Drop packet if bdd2 drops"),
    clEnumValEnd));


//handle forwarding device conflict
llvm::cl::opt<ForwardingDevPolicyOpt> ForwardingDevPolicy("fwd_device", 
llvm::cl::Required, llvm::cl::desc("Forwarding device conflict resolution:"),
  llvm::cl::values(
    clEnumVal(dev1, "Prioritize bdd1 devices"),
    clEnumVal(dev2, "Prioritize bdd2 devices"),
    clEnumValEnd));

} // namespace



/**
 * Clone a node, but without any links to other nodes
 *
 * @param node Shared pointer to a node
 * @return Shared pointer to a clone of the node
 */
BDD::BDDNode_ptr dupNode(BDD::BDDNode_ptr node){
  switch(node->get_type()){
    case BDD::Node::NodeType::BRANCH:
    {
      auto original = static_cast<BDD::Branch*>(node.get());
      auto clone = std::make_shared<BDD::Branch>(original->get_id(), original->get_condition());
      clone->set_constraints(node->get_constraints());
      return clone;
    }
    case BDD::Node::NodeType::CALL:
    {
      auto original = static_cast<BDD::Call*>(node.get());
      auto clone = std::make_shared<BDD::Call>(original->get_id(), original->get_call());
      clone->set_constraints(node->get_constraints());
      return clone;
    }
    case BDD::Node::NodeType::RETURN_PROCESS:
    {
      auto original = static_cast<BDD::ReturnProcess*>(node.get());
      auto clone = std::make_shared<BDD::ReturnProcess>(original->get_id(), original->get_return_value(), original->get_return_operation());
      clone->set_constraints(node->get_constraints());
      return clone;
    }
    default:
      assert(false && "Cannot duplicate unkown type of node.");
      break;
  }
}



/**
 * Insert <after> node after <root> node.
 *
 * @param root The node where it will be inserted the new node
 * @param after The node that will be inserted after <root>
 */
void insertAfter(BDD::BDDNode_ptr& root, BDD::BDDNode_ptr& after, bool onTrue = true){
  assert(after);
  assert(root);

  BDD::BDDNode_ptr next_root;
  if(root->get_type() == BDD::Node::NodeType::BRANCH){
    auto branch = static_cast<BDD::Branch*>(root.get());
    next_root = onTrue ? root->get_next() : branch->get_on_false();
  } else {
    next_root = root->get_next();
  }

  //root <-> after
  after->replace_prev(root);
  if(root->get_type() == BDD::Node::NodeType::BRANCH){
    auto branch = static_cast<BDD::Branch*>(root.get());
    if(onTrue) branch->replace_on_true(after); else branch->replace_on_false(after);
  } else {
    root->replace_next(after);
  }
  
  //after <-> root.next
  if(next_root){
    next_root->replace_prev(after);
    if(after->get_type() == BDD::Node::NodeType::BRANCH){
      auto branch = static_cast<BDD::Branch*>(after.get());
      branch->replace_on_true(next_root);
      branch->replace_on_false(next_root->clone(true));
    } else {
      after->replace_next(next_root);
    }
  }

}


bool node_equals(BDD::BDDNode_ptr n1, BDD::BDDNode_ptr n2){

  if(n1->get_type() != n2->get_type())
    return false;

  if(n1->get_constraints()[0].size() != n2->get_constraints()[0].size())
    return false;

  auto c_n1 = n1->get_constraints()[0].begin();
  auto c_n2 = n2->get_constraints()[0].begin();

  for(c_n1, c_n2; c_n1 != n1->get_constraints()[0].end(); c_n1++, c_n2++)
    if(!BDD::solver_toolbox.are_exprs_always_equal(*c_n1, *c_n2))
      return false;

  switch(n2->get_type()){
    case BDD::Node::NodeType::RETURN_PROCESS:
    {
      auto rp1 = static_cast<BDD::ReturnProcess*>(n1.get());
      auto rp2 = static_cast<BDD::ReturnProcess*>(n2.get());

      //global
      return (rp1->get_return_operation() == rp2->get_return_operation()) &&
              (rp1->get_return_value() == rp2->get_return_value());
    }
    case BDD::Node::NodeType::BRANCH:
    {
      auto b1 = static_cast<BDD::Branch*>(n1.get());
      auto b2 = static_cast<BDD::Branch*>(n2.get());

      //TODO: branch from packet and from data structures
      return BDD::solver_toolbox.are_exprs_always_equal(b1->get_condition(), b2->get_condition());
    }
    case BDD::Node::NodeType::CALL:
    {
      auto c1 = static_cast<BDD::Call*>(n1.get());
      auto c2 = static_cast<BDD::Call*>(n2.get());

      //global
      if(c1->get_call().function_name == "packet_borrow_next_chunk")
        return BDD::solver_toolbox.are_exprs_always_equal(
            c1->get_call().args["length"].expr,
            c2->get_call().args["length"].expr);

      //global
      if(c1->get_call().function_name == "packet_return_chunk"){
          auto out_c1 = c1->get_call().args["the_chunk"].in;
          auto out_c2 = c2->get_call().args["the_chunk"].in;
          return out_c1->getWidth() == out_c2->getWidth();
      }

      return BDD::solver_toolbox.are_calls_equal(c1->get_call(), c2->get_call()) &&
              c1->get_from() == c2->get_from();
    }
    default:
      assert(false && "Cannot compare two nodes with unkown types.");
      break;
  }
}



//return process being added before another return process
void addNode(BDD::BDDNode_ptr& root, BDD::BDDNode_ptr& new_node){

  if(node_equals(root, new_node))
    return;

  switch (root->get_type()) {
    case BDD::Node::NodeType::BRANCH: {
      
      auto branch = static_cast<BDD::Branch *>(root.get());
      std::cerr << "[Branch] prev->" << branch->get_prev() << " on_true->"
                << branch->get_on_true() << " on_false->"
                << branch->get_on_false() << std::endl;
      klee::ConstraintManager on_true_path_contrs;
      on_true_path_contrs.addConstraint(branch->get_condition());
      klee::ConstraintManager on_false_path_contrs;
      on_false_path_contrs.addConstraint(BDD::solver_toolbox.exprBuilder->Not(branch->get_condition()));

      // on true
      if (BDD::solver_toolbox.are_constraints_compatible(on_true_path_contrs,
                                                        new_node->get_constraints()[0])) {
        auto next_root = branch->get_on_true();
        if(!next_root || (next_root->get_type() == BDD::Node::NodeType::RETURN_PROCESS)){
          insertAfter(root, new_node);
        } else {
          addNode(next_root, new_node);
        }
      }

      // on false
      if (BDD::solver_toolbox.are_constraints_compatible(on_false_path_contrs,
                                                        new_node->get_constraints()[0])) {
        new_node = dupNode(new_node);
        auto next_root = branch->get_on_false();
        if(!next_root || (next_root->get_type() == BDD::Node::NodeType::RETURN_PROCESS)){
          insertAfter(root, new_node, false);
        } else {
          addNode(next_root, new_node);
        }
      }

      break;
    }
    case BDD::Node::NodeType::CALL:
    {
      auto call = static_cast<BDD::Call *>(root.get());
      std::cerr << "[Call] prev->" << call->get_prev() << " next->" << call->get_next() << std::endl;
      auto next_root = call->get_next();
      if(!next_root || (next_root->get_type() == BDD::Node::NodeType::RETURN_PROCESS)){
        insertAfter(root, new_node, false);
      } else {
        addNode(next_root, new_node);
      }

      break;
    }
    case BDD::Node::NodeType::RETURN_PROCESS:
    default:
      assert(false);
      break;
    };
}



int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  BDD::solver_toolbox.build();
  BDD::PathExplorer explorer;
  int all_comb = 0;
  int compat_combs = 0;

  BDD::BDD bdd1(BDD1);
  BDD::BDD bdd2(BDD2);
  
  std::vector<BDD::bdd_path_t*> bdd1_paths;
  std::vector<BDD::bdd_path_t*> bdd2_paths;

  explorer.getPathsProcess(bdd1, bdd1_paths);
  explorer.getPathsProcess(bdd2, bdd2_paths);

  for(auto p1 : bdd1_paths){
    for(auto p2 : bdd2_paths){
      all_comb++;
      if(explorer.arePathsCompatible(p1, p2)){
        compat_combs++;
      }
    }
  }

  std::cerr << "Number combinations: " << all_comb << std::endl;
  std::cerr << "Compatible combinations: " << compat_combs << std::endl;

  return 0;
}