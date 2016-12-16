#include "CMMInterpreter.h"
#include "NativeFunctions.h"
#include <cassert>

using namespace cmm;

void CMMInterpreter::interpret() {
  for (auto &Stmt : TopLevelBlock.getStatementList()) {
    ExecutionResult Res = executeStatement(&TopLevelEnv, Stmt.get());
    if (Res.Kind != ExecutionResult::NormalStatementResult)
      RuntimeError("unbounded break/continue/return");
  }
}

void CMMInterpreter::addNativeFunctions() {
  NativeFunctionMap["print"] = cvm::NativePrint;
  NativeFunctionMap["println"] = cvm::NativePrintln;
  NativeFunctionMap["system"] = cvm::NativeSystem;
}

void CMMInterpreter::RuntimeError(const std::string &Msg) {
  std::cerr << "Runtime Error: " << Msg << std::endl;
  std::exit(EXIT_FAILURE);
}

CMMInterpreter::ExecutionResult
CMMInterpreter::executeBlock(VariableEnv *OuterEnv, const BlockAST *Block) {
  ExecutionResult Res;
  VariableEnv CurrentEnv(OuterEnv);

  for (auto &Statement : Block->getStatementList()) {
    Res = executeStatement(&CurrentEnv, Statement.get());
    if (Res.Kind != ExecutionResult::NormalStatementResult)
      break;
  }
  return Res;
}

CMMInterpreter::ExecutionResult
CMMInterpreter::executeStatement(VariableEnv *Env, const StatementAST *Stmt) {
  ExecutionResult Res;

  switch (Stmt->getKind()) {
  default:
    RuntimeError("unknown statement kind");
    break;
  case StatementAST::ExprStatement:
    Res = executeExprStatement(Env,
                               static_cast<const ExprStatementAST *>(Stmt));
    break;
  case StatementAST::BlockStatement:
    Res = executeBlock(Env, static_cast<const BlockAST *>(Stmt));
    break;
  case StatementAST::IfStatement:
    //Res = executeIfStatement(Env, static_cast<const IfStatementAST *>(Stmt));
    break;
  case StatementAST::ReturnStatement:
    Res = executeReturnStatement(Env,
                                 static_cast<const ReturnStatementAST *>(Stmt));
    break;
  case StatementAST::WhileStatement:
  case StatementAST::ForStatement:
  case StatementAST::ContinueStatement:
  case StatementAST::BreakStatement:
  case StatementAST::DeclarationStatement:
    RuntimeError("single declaration should not be used by user");
  case StatementAST::DeclarationListStatement:
    Res = executeDeclarationList(Env,
                                 static_cast<const DeclarationListAST *>(Stmt));
    break;
  }

  if (Res.Kind != ExecutionResult::NormalStatementResult) {
    //todo
  }
  return Res;
}

CMMInterpreter::ExecutionResult
CMMInterpreter::executeIfStatement(VariableEnv *Env, IfStatementAST *Stmt) {
  // todo
  return ExecutionResult();
}

CMMInterpreter::ExecutionResult
CMMInterpreter::executeExprStatement(VariableEnv *Env,
                                     const ExprStatementAST *Stmt) {
  evaluateExpression(Env, Stmt->getExpression());
  return ExecutionResult();
}

CMMInterpreter::ExecutionResult
CMMInterpreter::executeReturnStatement(VariableEnv *Env,
                                       const ReturnStatementAST *Stmt) {
  ExecutionResult Res(ExecutionResult::ReturnStatementResult);
  if (auto ReturnValueExpr = Stmt->getReturnValue())
    Res.ReturnValue = evaluateExpression(Env, ReturnValueExpr);
  return Res;
}

CMMInterpreter::ExecutionResult
CMMInterpreter::executeDeclarationList(VariableEnv *Env,
                                       const DeclarationListAST *DeclList) {
  for (auto &Decl : DeclList->getDeclarationList()) {
    executeDeclaration(Env, Decl.get());
  }
  return ExecutionResult();
}

CMMInterpreter::ExecutionResult
CMMInterpreter::executeDeclaration(VariableEnv *Env,
                                   const DeclarationAST *Decl) {
  const std::string& Name = Decl->getName();

  if (Env->contains(Name)) {
    RuntimeError("variable `" + Decl->getName() +
        "' is already defined in current scope");
  }

  if (Decl->isArray()) {
    RuntimeError("unimplemented!"); // TODO
  }

  // Now it's a normal variable.
  if (Decl->getInitializer()) {
    cvm::BasicValue Val = evaluateExpression(Env, Decl->getInitializer());

    if (Val.Type != Decl->getType()) {
      if (Decl->getType() == cvm::DoubleType && Val.isInt()) {
        Val.Type = cvm::DoubleType;
        Val.DoubleVal = static_cast<double>(Val.IntVal);
      } else {
        RuntimeError("variable `" + Name + "' is declared to be " +
            cvm::TypeToStr(Decl->getType()) + ", but is initialized to be " +
            cvm::TypeToStr(Val.Type));
      }
    }
    Env->VarMap.emplace(std::make_pair(Name, Val));
  } else {
    Env->VarMap.emplace(std::make_pair(Name, cvm::BasicType(Decl->getType())));
  }

  return ExecutionResult();
}

cvm::BasicValue
CMMInterpreter::evaluateExpression(VariableEnv *Env,
                                   const ExpressionAST *Expr) {
  cvm::BasicValue Value;

  switch (Expr->getKind()) {
  default:
    RuntimeError("unknown expression kind");
  case ExpressionAST::IntExpression:
    return cvm::BasicValue(static_cast<const IntAST *>(Expr)->getValue());
  case ExpressionAST::DoubleExpression:
    return cvm::BasicValue(static_cast<const DoubleAST *>(Expr)->getValue());
  case ExpressionAST::BoolExpression:
    return cvm::BasicValue(static_cast<const BoolAST *>(Expr)->getValue());
  case ExpressionAST::StringExpression:
    return cvm::BasicValue(static_cast<const StringAST *>(Expr)->getValue());
  case ExpressionAST::IdentifierExpression:
    return evaluateIdentifierExpr(Env,
                                  static_cast<const IdentifierAST *>(Expr));
  case ExpressionAST::FunctionCallExpression:
    return evaluateFunctionCallExpr(Env,
                                    static_cast<const FunctionCallAST *>(Expr));
  case ExpressionAST::BinaryOperatorExpression:
    return evaluateBinaryOpExpr(Env,
                                static_cast<const BinaryOperatorAST *>(Expr));
  case ExpressionAST::UnaryOperatorExpression:
    RuntimeError("unimplemented"); //TODO
  }
}

cvm::BasicValue
CMMInterpreter::evaluateFunctionCallExpr(VariableEnv *Env,
                                         const FunctionCallAST *FuncCall) {
  auto UserFuncIt = UserFunctionMap.find(FuncCall->getCallee());
  if (UserFuncIt != UserFunctionMap.end()) {
    auto Args(evaluateArgumentList(Env, FuncCall->getArguments()));
    return callUserFunction(UserFuncIt->second, Args);
  }

  auto NativeFuncIt = NativeFunctionMap.find(FuncCall->getCallee());
  if (NativeFuncIt != NativeFunctionMap.end()) {
    auto Args(evaluateArgumentList(Env, FuncCall->getArguments()));
    return callNativeFunction(NativeFuncIt->second, Args);
  }

  RuntimeError("function `" + FuncCall->getCallee() + "' is undefined");
  return cvm::BasicValue(); // Make the compiler happy.
}


cvm::BasicValue
CMMInterpreter::evaluateIdentifierExpr(VariableEnv *Env,
                                       const IdentifierAST *IdExpr) {
  return searchVariable(Env, IdExpr->getName())->second;
}

cvm::BasicValue
CMMInterpreter::evaluateBinaryOpExpr(VariableEnv *Env,
                                     const BinaryOperatorAST *Expr) {
  // Is it an assignment?
  if (Expr->getOpKind() == Expr->Assign) {
    const std::string &Name =
        static_cast<IdentifierAST *>(Expr->getLHS())->getName();
    return evaluateAssignment(Env, Name, Expr->getRHS());
  }

  if(Expr->getOpKind() == Expr->Index) {
    RuntimeError("array unimplemented!");
  }

  cvm::BasicValue LHS = evaluateExpression(Env, Expr->getLHS());
  cvm::BasicValue RHS = evaluateExpression(Env, Expr->getRHS());
  return evaluateBinaryCalc(Expr->getOpKind(), LHS, RHS);
}

cvm::BasicValue
CMMInterpreter::evaluateBinaryCalc(BinaryOperatorAST::OperatorKind OpKind,
                                   cvm::BasicValue LHS, cvm::BasicValue RHS) {
  switch (OpKind) {
  default:
    RuntimeError("unknown binary operator kind (code :" +
        std::to_string(OpKind) + ")");
  case BinaryOperatorAST::Add:
    if (LHS.isString() || RHS.isString())
      return cvm::BasicValue(LHS.toString() + RHS.toString());
    /* fall Through */
  case BinaryOperatorAST::Minus:
  case BinaryOperatorAST::Multiply:
  case BinaryOperatorAST::Division:
    return evaluateBinArith(OpKind, LHS, RHS);
  case BinaryOperatorAST::LogicalAnd:
  case BinaryOperatorAST::LogicalOr:
    return evaluateBinLogic(OpKind, LHS, RHS);
  case BinaryOperatorAST::Less:
  case BinaryOperatorAST::LessEqual:
  case BinaryOperatorAST::Equal:
  case BinaryOperatorAST::Greater:
  case BinaryOperatorAST::GreaterEqual:
    return evaluateBinRelation(OpKind, LHS, RHS);
  case BinaryOperatorAST::BitwiseAnd:
  case BinaryOperatorAST::BitwiseOr:
  case BinaryOperatorAST::BitwiseXor:
  case BinaryOperatorAST::LeftShift:
  case BinaryOperatorAST::RightShift:
    return evaluateBinBitwise(OpKind, LHS, RHS);
  case BinaryOperatorAST::Assign:
  case BinaryOperatorAST::Index:
    RuntimeError("assignment/index should be handled in evaluateBinaryOpExpr");
  }
}


std::map<std::string, cvm::BasicValue>::iterator
CMMInterpreter::searchVariable(VariableEnv *Env, const std::string &Name) {
  for (VariableEnv *E = Env; E != nullptr; E = E->OuterEnv) {
    std::map<std::string, cvm::BasicValue>::iterator It = E->VarMap.find(Name);
    if (It != E->VarMap.end())
      return It;
  }
  RuntimeError("variable `" + Name + "' is undefined");
  return Env->VarMap.end(); // Make the compiler happy.
}

std::list<cvm::BasicValue>
CMMInterpreter::evaluateArgumentList(VariableEnv *Env,
                                     const std::list<std::unique_ptr<ExpressionAST>> &Args) {

  std::list<cvm::BasicValue> Res;
  for (auto &P : Args)
    Res.emplace_back(evaluateExpression(Env, P.get()));
  return Res;
}

cvm::BasicValue
CMMInterpreter::callNativeFunction(NativeFunction &Function,
                                   std::list<cvm::BasicValue> &Args) {
  return Function(Args);
}

cvm::BasicValue
CMMInterpreter::callUserFunction(FunctionDefinitionAST &Function,
                                 std::list<cvm::BasicValue> &Args) {
  if (Args.size() != Function.getParameterCount()) {
    RuntimeError("Function `" + Function.getName() + "' expects " +
        std::to_string(Function.getParameterCount()) + " parameter(s), " +
        std::to_string(Args.size()) + " argument(s) provided");
  }

  VariableEnv FuncEnv(&TopLevelEnv);

  auto ParaIt = Function.getParameterList().cbegin();
  auto ParaEnd = Function.getParameterList().cend();
  for (cvm::BasicValue &Arg : Args) {
    if (ParaIt->getType() != Arg.Type) {
      RuntimeError("in function `" + Function.getName() + "', parameter `" +
          ParaIt->getName() + "' has type " + cvm::TypeToStr(ParaIt->getType()) +
          ", but argument has type " + cvm::TypeToStr(Arg.Type));
    }
    FuncEnv.VarMap[ParaIt->getName()] = Arg;
    ++ParaIt;
  }

  assert(ParaIt == ParaEnd);

  ExecutionResult Result = executeStatement(&FuncEnv, Function.getStatement());
  if (Result.ReturnValue.Type != Function.getType()) {
    RuntimeError("function `" + Function.getName() + "' ought to return " +
        cvm::TypeToStr(Function.getType()) + ", but got " +
        cvm::TypeToStr(Result.ReturnValue.Type));
  }
  return Result.ReturnValue;
}

cvm::BasicValue
CMMInterpreter::evaluateAssignment(VariableEnv *Env, const std::string &Name,
                                   const ExpressionAST *Expr) {
  cvm::BasicValue &LHS = searchVariable(Env, Name)->second;
  cvm::BasicValue RHS = evaluateExpression(Env, Expr);

  if (LHS.Type != RHS.Type) {
    if (LHS.isDouble() && RHS.isInt()) {
      LHS.DoubleVal = RHS.IntVal;
      return LHS;
    } else {
      RuntimeError("assignment to " + cvm::TypeToStr(LHS.Type) + " variable `" +
          Name + "' with " + cvm::TypeToStr(RHS.Type) + " expression");
    }
  }
  return LHS = RHS;
}
