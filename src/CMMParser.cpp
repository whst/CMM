#include "CMMParser.h"
#include <cassert>

using namespace cmm;

bool CMMParser::parse() {
  Lex();
  while (!Lexer.isOneOf(Token::Eof, Token::Error))
    if (parseTopLevel())
      return true;
  return false;
}

void CMMParser::dumpAST() const {

  if (FunctionDefinition.empty()) {
    std::cout << "Note: no user-defined function\n\n";
  } else {
    std::cout << "{---- Function definitions ----}\n";
    for (const auto &F : FunctionDefinition) {
      F.second.dump();
      std::cout << "\n";
    }
  }
  std::cout << "\n";

  if (InfixOpDefinition.empty()) {
    std::cout << "Note: no user-defined infix operator\n\n";
  } else {
    std::cout << "{-----  Infix operators   -----}\n";
    for (const auto &I : InfixOpDefinition) {
      I.second.dump();
      std::cout << "\n";
    }
  }
  std::cout << "\n";

  if (TopLevelBlock.getStatementList().empty()) {
    std::cout << "Note: statement list is empty\n";
  } else {
    std::cout << "{----  Statement list AST  ----}\n";
    for (const auto &S : TopLevelBlock.getStatementList()) {
      S->dump();
      std::cout << "\n";
    }
  }
}

/// \brief Parse top level entities.
/// TopLevel ::= infixOperatorDefinition
/// TopLevel ::= functionDeclaration
/// TopLevel ::= DeclarationStatement
/// TopLevel ::= Statement
bool CMMParser::parseTopLevel() {
  switch (getKind()) {
  default: {
    std::unique_ptr<StatementAST> Statement;
    if (parseStatement(Statement))
      return true;
    TopLevelBlock.addStatement(std::move(Statement));
    return false;
  }
  case Token::Kw_infix:
    return parseInfixOpDefinition();
  case Token::Kw_void:
    return parseFunctionDefinition();
  case Token::Kw_int: case Token::Kw_bool:
  case Token::Kw_double: case Token::Kw_string: {
    // We don't know if it's a function definition or variable declaration.
    // They all start with Type Identifier
    cvm::BasicType Type;
    if (parseTypeSpecifier(Type))
      return true;

    LocTy Loc = Lexer.getLoc();

    if (Lexer.isNot(Token::Identifier))
      return Error("expect identifier after type");
    std::string Name = Lexer.getStrVal();
    Lex();  // Eat the identifier.

    if (Lexer.is(Token::LParen))
      return parseFunctionDefinition(Type, Name); // It's function definition.

    // It's a variable declaration.
    Lexer.seekLoc(Loc);
    Lex();
    std::unique_ptr<StatementAST> DeclStatement;
    if (parseDeclarationStatement(Type, DeclStatement))
      return true;
    TopLevelBlock.addStatement(std::move(DeclStatement));
    return false;
  }
  }
}

/// \brief Parse an infix operator definition.
/// infixOpDefinition ::= Kw_infix [Integer] Id infixOp Id Statement
/// infixOpDefinition ::= Kw_infix [Integer] Id infixOp Id ["="] ExprStatement
/// E.g., infix [12] a@b [=] a * b;
bool CMMParser::parseInfixOpDefinition() {
  assert(Lexer.is(Token::Kw_infix));
  LocTy Loc = Lexer.getLoc();
  Lex();  // Eat the 'infix'.

  int Precedence;

  if (Lexer.is(Token::Integer)) {
    Precedence = Lexer.getIntVal();
    Lex();  // Eat the int.
  } else {
    Precedence = InfixOpDefinitionAST::DefaultPrecedence;
  }

  if (Lexer.isNot(Token::Identifier))
    return Error("left hand operand name for infix operator expected");
  std::string LHS = Lexer.getStrVal();
  Lex();  // eat the LHS operand identifier.

  if (Lexer.isNot(Token::InfixOp))
    return Error("symbol of infix operator expected");
  std::string Symbol = Lexer.getStrVal();
  Lex();  // eat the infix operator.

  if (Lexer.isNot(Token::Identifier))
    return Error("right hand operand name for infix operator expected");
  std::string RHS = Lexer.getStrVal();
  Lex();  // eat the RHS operand identifier.

  std::unique_ptr<StatementAST> Statement;
  bool Err;
  if (Lexer.is(Token::Equal)) {
    Lex();  // eat the '='
    Err = parseExprStatement(Statement);
  } else {
    Err = parseStatement(Statement);
  }
  if (Err)
    return true;

  if (!BinOpPrecedence.emplace(Symbol,
                               static_cast<int8_t>(Precedence)).second) {
    Warning(Loc, "infix operator " + Symbol + " overrides another");
  }
  InfixOpDefinition.emplace(Symbol, InfixOpDefinitionAST(Symbol, LHS, RHS,
                                                         std::move(Statement)));
  return false;
}

/// \brief Parse a function definition.
/// functionDefinition ::= typeSpecifier identifier _functionDefinition
/// functionDefinition ::= typeSpecifier identifier _functionDefinition
bool CMMParser::parseFunctionDefinition() {
  cvm::BasicType RetType;
  if (parseTypeSpecifier(RetType))
    return true;

  if (Lexer.isNot(Token::Identifier))
    return Error("expect identifier in function definition");

  std::string Identifier = Lexer.getStrVal();
  Lex();  // eat the identifier of function.

  return parseFunctionDefinition(RetType, Identifier);
}

/// \brief Parse a function definition body.
/// _functionDefinition ::= "(" ")" Statement
/// _functionDefinition ::= "(" parameterList ")" Statement
bool CMMParser::parseFunctionDefinition(cvm::BasicType RetType,
                                        const std::string &Name) {
  assert(Lexer.is(Token::LParen) && "parseFunctionDefinition: unknown token");
  LocTy Loc = Lexer.getLoc();
  Lex();  // Eat LParen '('.

  std::list<Parameter> ParameterList;
  if (Lexer.isNot(Token::RParen))
    parseParameterList(ParameterList);
  if (Lexer.isNot(Token::RParen))
    return Error("right parenthesis expected");
  Lex();  // Eat RParen ')'.

  std::unique_ptr<StatementAST> Statement;
  if (parseStatement(Statement))
    return true;

  FunctionDefinitionAST FuncDef(Name, RetType, std::move(ParameterList),
                                std::move(Statement));
  if (!FunctionDefinition.emplace(Name, std::move(FuncDef)).second) {
    Warning(Loc, "function `" + Name + "' overrides another one");
  }
  return false;
}

/// \brief Parse a parameter list
/// parameterList ::= "void"
/// parameterList ::= TypeSpecifier Identifier ("," TypeSpecifier Identifier)*
bool CMMParser::parseParameterList(std::list<Parameter> &ParameterList) {
  if (Lexer.is(Token::Kw_void)) {
    Lex();
    return false;
  }
  for (;;) {
    std::string Identifier;
    cvm::BasicType Type;
    LocTy Loc;

    if (parseTypeSpecifier(Type))
      return true;

    Loc = Lexer.getLoc();
    if (Lexer.is(Token::Identifier)) {
      Identifier = Lexer.getStrVal();
      Lex();  // Eat the identifier.
    } else {
      Warning("missing identifier after type");
    }
    ParameterList.emplace_back(Identifier, Type, Loc);

    if (Lexer.isNot(Token::Comma))
      break;
    Lex();  // Eat the comma.
  }
  return false;
}

/// \brief Parse a block as a statement.
/// block ::= "{" statement* "}"
bool CMMParser::parseBlock(std::unique_ptr<StatementAST> &Res) {
  CurrentBlock = new BlockAST(CurrentBlock);
  Res.reset(CurrentBlock);

  assert(Lexer.is(Token::LCurly) && "first token in parseBlock()");
  Lex(); // eat the LCurly '{'

  while (Lexer.isNot(Token::RCurly)) {
    std::unique_ptr<StatementAST> Statement;
    if (parseStatement(Statement))
      return true;
    CurrentBlock->addStatement(std::move(Statement));
  }

  assert(CurrentBlock == Res.get() && "block mismatch");
  CurrentBlock = CurrentBlock->getOuterBlock();

  Lex(); // eat the RCurly '}'
  return false;
}

/// \brief Parse a typeSpecifier.
/// typeSpecifier ::= "bool" | "int" | "double" | "void" | "string"
bool CMMParser::parseTypeSpecifier(cvm::BasicType &Type) {
  switch (getKind()) {
  default:                return Error("unknown type specifier");
  case Token::Kw_bool:    Type = cvm::BoolType; break;
  case Token::Kw_int:     Type = cvm::IntType; break;
  case Token::Kw_double:  Type = cvm::DoubleType; break;
  case Token::Kw_void:    Type = cvm::VoidType; break;
  case Token::Kw_string:  Type = cvm::StringType; break;
  }
  Lex();
  return false;
}

/// \brief Parse an optional argument list.
/// OptionalArgList ::= epsilon
/// OptionalArgList ::= argumentList
bool CMMParser::parseOptionalArgList(std::list<std::unique_ptr<ExpressionAST>>
                                     &ArgList) {
  if (Lexer.is(Token::RParen))
    return false;
  return parseArgumentList(ArgList);
}

/// \brief Parse an argument list.
/// argumentList ::= Expression ("," Expression)*
bool CMMParser::parseArgumentList(std::list<std::unique_ptr<ExpressionAST>>
                                  &ArgList) {
  for (;;) {
    std::unique_ptr<ExpressionAST> Expression;
    if (parseExpression(Expression))
      return true;
    ArgList.emplace_back(std::move(Expression));
    if (Lexer.isNot(Token::Comma))
      break;
    Lex(); // Eat the comma.
  }
  return false;
}

/// \brief Parse an empty statement.
/// EmptyStatement ::= ";"
bool CMMParser::parseEmptyStatement(std::unique_ptr<StatementAST> &Res) {
  Warning("empty statement");
  Res = nullptr;
  Lex(); // eat the semicolon;
  return false;
}

/// \brief Parse a statement
/// Statement ::= Block
/// Statement ::= IfStatement
/// Statement ::= WhileStatement
/// Statement ::= ForStatement
/// Statement ::= ReturnStatement
/// Statement ::= BreakStatement
/// Statement ::= ContinueStatement
/// Statement ::= EmptyStatement
/// Statement ::= DeclarationStatement
/// Statement ::= ExprStatement
bool CMMParser::parseStatement(std::unique_ptr<StatementAST> &Res) {
  switch (getKind()) {
  default:
    return Error("unexpected token in statement");
  case Token::LCurly:       return parseBlock(Res);
  case Token::Kw_if:        return parseIfStatement(Res);
  case Token::Kw_while:     return parseWhileStatement(Res);
  case Token::Kw_for:       return parseForStatement(Res);
  case Token::Kw_return:    return parseReturnStatement(Res);
  case Token::Kw_break:     return parseBreakStatement(Res);
  case Token::Kw_continue:  return parseContinueStatement(Res);
  case Token::Semicolon:    return parseEmptyStatement(Res);
  case Token::Kw_bool:
  case Token::Kw_int:
  case Token::Kw_double:
  case Token::Kw_string:
    return parseDeclarationStatement(Res);
  case Token::Kw_void:
    return Error("`void' only appears before function definition");
  case Token::LParen:   case Token::Identifier:
  case Token::Double:   case Token::String:
  case Token::Boolean:  case Token::Integer:
  case Token::Plus:     case Token::Minus:
  case Token::Tilde:    case Token::Exclaim:
    return parseExprStatement(Res);
  }
}

/// \brief Parse an expression.
/// expression ::= primaryExpr BinOpRHS*
bool CMMParser::parseExpression(std::unique_ptr<ExpressionAST> &Res) {
  return parsePrimaryExpression(Res) || parseBinOpRHS(1, Res);
}

int8_t CMMParser::getBinOpPrecedence() {
  switch (getKind()) {
  default:              return -1;
  case Token::Equal:    return 1;
  case Token::PipePipe: return 2;
  case Token::AmpAmp:   return 3;
  case Token::Pipe:     return 4;
  case Token::Caret:    return 5;
  case Token::Amp:      return 6;
  case Token::EqualEqual:
  case Token::ExclaimEqual:
    return 7;
  case Token::Less:
  case Token::LessEqual:
  case Token::Greater:
  case Token::GreaterEqual:
    return 8;
  case Token::LessLess:
  case Token::GreaterGreater:
    return 9;
  case Token::Plus:
  case Token::Minus:
    return 10;
  case Token::Star:
  case Token::Slash:
  case Token::Percent:
    return 11;
  case Token::InfixOp: {
    auto It = BinOpPrecedence.find(Lexer.getStrVal());
    if (It != BinOpPrecedence.end())
      return It->second;
    break;
  }
  }
  return -1;
}

/// \brief Parse a paren expression and return it.
/// parenExpr ::= "(" expression ")"
bool CMMParser::parseParenExpression(std::unique_ptr<ExpressionAST> &Res) {
  Lex(); // eat the '('.
  if (parseExpression(Res))
    return true;
  if (Lexer.isNot(Token::RParen))
    return Error("expected ')' in parentheses expression");
  Lex(); // eat the ')'.
  return false;
}

/// \brief Parse a primary expression and return it.
///  primaryExpr ::= parenExpr
///  primaryExpr ::= identifierExpr
///  primaryExpr ::= identifierExpr ("[" Expression "]")+
///  primaryExpr ::= constantExpr
///  primaryExpr ::= "~","+","-","!" primaryExpr
bool CMMParser::parsePrimaryExpression(std::unique_ptr<ExpressionAST> &Res) {
  UnaryOperatorAST::OperatorKind UnaryOpKind;
  std::unique_ptr<ExpressionAST> Operand;

  switch (getKind()) {
  default:
    return Error("unexpected token in expression");

  case Token::LParen:
    return parseParenExpression(Res);

  case Token::Identifier:
    if (parseIdentifierExpression(Res))
      return true;
    while (Lexer.is(Token::LBrac)) {
      Lex(); // Eat the ']'.

      std::unique_ptr<ExpressionAST> IndexExpr, TmpRHS;
      if (parseExpression(IndexExpr))
        return true;

      if (Lexer.isNot(Token::RBrac))
        return Error("RBrac ']' expected in index expression");
      Lex(); // Eat the ']'.

      std::swap(Res, TmpRHS);
      Res.reset(new BinaryOperatorAST(
          BinaryOperatorAST::Index, std::move(TmpRHS), std::move(IndexExpr)));
    }
    return false;

  case Token::Integer:
  case Token::Double:
  case Token::String:
  case Token::Boolean:
    return parseConstantExpression(Res);

  case Token::Plus:     UnaryOpKind = UnaryOperatorAST::Plus; break;
  case Token::Minus:    UnaryOpKind = UnaryOperatorAST::Minus; break;
  case Token::Tilde:    UnaryOpKind = UnaryOperatorAST::BitwiseNot; break;
  case Token::Exclaim:  UnaryOpKind = UnaryOperatorAST::LogicalNot; break;
  }

  Lex(); // Eat the operator: +,-,~,!
  if (parsePrimaryExpression(Operand))
    return true;
  Res = UnaryOperatorAST::tryFoldUnaryOp(UnaryOpKind, std::move(Operand));
  return false;
}

/// \brief Parse the right hand side of a binary expression
/// if the current binOp's precedence is greater or equal to ExprPrec.
bool CMMParser::parseBinOpRHS(int8_t ExprPrec,
                              std::unique_ptr<ExpressionAST> &Res) {
  std::unique_ptr<ExpressionAST> RHS;

  // Handle assignment expression first.
  if (Lexer.getTok().is(Token::Equal)) {
    Lex();
    if (parseExpression(RHS))
      return true;
    Res = BinaryOperatorAST::create(Token::Equal,
                                    std::move(Res), std::move(RHS));
    return false;
  }
  for (;;) {
    Token::TokenKind TokenKind = getKind();

    // If this is a binOp, find its precedence.
    int8_t TokPrec = getBinOpPrecedence();
    // If the next token is lower precedence than we are allowed to eat,
    // return successfully with what we ate already.
    if (TokPrec < ExprPrec)
      return false;

    // Save the potential symbol before lex.
    std::string Symbol = Lexer.getStrVal();
    // Eat the binary operator.
    Lex();
    // Eat the next primary expression.
    if (parsePrimaryExpression(RHS))
      return true;

    // If BinOp binds less tightly with RHS than the operator after RHS, let
    // the pending operator take RHS as its LHS.
    int8_t NextPrec = getBinOpPrecedence();
    if (TokPrec < NextPrec && parseBinOpRHS(++TokPrec, RHS))
      return true;

    // Merge LHS and RHS according to operator.
    if (TokenKind == Token::InfixOp)
      Res.reset(new InfixOpExprAST(Symbol, std::move(Res), std::move(RHS)));
    else
      Res = BinaryOperatorAST::tryFoldBinOp(TokenKind, std::move(Res),
                                             std::move(RHS));
  }
}



/// \brief Parse an identifier expression
/// identifierExpression ::= identifier
/// identifierExpression ::= identifier  "("  optionalArgList  ")"
bool CMMParser::parseIdentifierExpression(std::unique_ptr<ExpressionAST> &Res) {
  assert(Lexer.is(Token::Identifier) &&
      "parseIdentifierExpression: unknown token");

  std::string Identifier = Lexer.getStrVal();
  Lex();  // eat the identifier

  LocTy ExclaimLoc;
  bool Dynamic;
  if ((Dynamic = Lexer.is(Token::Exclaim))) {
    ExclaimLoc = Lexer.getLoc();
    Lex();  // eat the '!'
  }

  if (Lexer.is(Token::LParen)) {
    Lex();  // eat the '('

    std::list<std::unique_ptr<ExpressionAST>> Args;
    if (parseOptionalArgList(Args))
      return true;

    if (Lexer.isNot(Token::RParen))
      return Error("expect ')' in function call");
    Lex(); // eat the ')'
    Res.reset(new FunctionCallAST(Identifier, std::move(Args), Dynamic));
  } else {
    if (Dynamic)
      Warning(ExclaimLoc, "trailing `!' is ignored in identifier");
    Res.reset(new IdentifierAST(Identifier));
  }

  return false;
}

/// \brief Parse a constant expression.
/// constantExpr ::= IntExpression
/// constantExpr ::= DoubleExpression
/// constantExpr ::= BoolExpression
/// constantExpr ::= StringExpression
bool CMMParser::parseConstantExpression(std::unique_ptr<ExpressionAST> &Res) {
  switch (getKind()) {
  default:  return Error("unknown token in literal constant expression");
  case Token::Integer:  Res.reset(new IntAST(Lexer.getIntVal())); break;
  case Token::Double:   Res.reset(new DoubleAST(Lexer.getDoubleVal())); break;
  case Token::Boolean:  Res.reset(new BoolAST(Lexer.getBoolVal())); break;
  case Token::String:   Res.reset(new StringAST(Lexer.getStrVal())); break;
  }
  Lex(); // eat the string,bool,int,double.
  return false;
}

/// \brief Parse an if statement.
/// ifStatement ::= "if"  "(" Expr ")"  Statement
/// ifStatement ::= "if"  "(" Expr ")"  Statement  "else"  Statement
bool CMMParser::parseIfStatement(std::unique_ptr<StatementAST> &Res) {
  std::unique_ptr<ExpressionAST> Condition;
  std::unique_ptr<StatementAST> StatementThen, StatementElse;

  assert(Lexer.is(Token::Kw_if) && "parseIfStatement: unknown token");
  Lex();  // eat 'if'.

  if (Lexer.isNot(Token::LParen))
    return Error("left parenthesis expected");
  Lex();  // eat LParen '('.

  if (parseExpression(Condition))
    return true;
  if (Lexer.isNot(Token::RParen))
    return Error("right parenthesis expected");
  Lex();  // eat RParen ')'.

  if (parseStatement(StatementThen))
    return true;

  // Parse the else branch is there is one.
  if (Lexer.is(Token::Kw_else)) {
    Lex();  // eat 'else'
    if (parseStatement(StatementElse))
      return true;
  }

  Res = IfStatementAST::create(std::move(Condition),
                               std::move(StatementThen),
                               std::move(StatementElse));
  return false;
}

/// \brief Parse a for statement.
/// forStatement ::= "for"  "("  Expr  ";"  Expr  ";"  Expr  ")"  Statement
bool CMMParser::parseForStatement(std::unique_ptr<StatementAST> &Res) {
  std::unique_ptr<ExpressionAST> Init, Condition, Post;
  std::unique_ptr<StatementAST> Statement;

  assert(Lexer.is(Token::Kw_for) && "parseIfStatement: unknown token");
  Lex();  // eat the 'for'.
  if (Lexer.isNot(Token::LParen))
    return Error("left parenthesis expected in for loop");
  Lex();  // eat the LParen '('.

  if (Lexer.isNot(Token::Semicolon) && parseExpression(Init))
    return true;
  if (Lexer.isNot(Token::Semicolon))
    return Error("missing semicolon for initial expression in for loop");
  Lex();  // eat the semicolon.

  if (Lexer.isNot(Token::Semicolon) && parseExpression(Condition))
    return true;
  if (Lexer.isNot(Token::Semicolon))
    return Error("missing semicolon for conditional expression in for loop");
  Lex();  // eat the semicolon.

  if (Lexer.isNot(Token::RParen) && parseExpression(Post))
    return true;
  if (Lexer.isNot(Token::RParen))
    return Error("missing semicolon for post expression in for loop");
  Lex();  // eat the ')'.

  if (parseStatement(Statement))
    return true;

  Res = ForStatementAST::create(std::move(Init), std::move(Condition),
                                std::move(Post), std::move(Statement));
  return false;
}

/// \brief Parse a while statement.
/// whileStatement ::= "while"  "("  Expression  ")"  Statement
bool CMMParser::parseWhileStatement(std::unique_ptr<StatementAST> &Res) {
  std::unique_ptr<ExpressionAST> Condition;
  std::unique_ptr<StatementAST> Statement;

  assert(Lexer.is(Token::Kw_while) &&
      "parseIfStatement: unknown token, 'while' expexted");
  Lex();  // eat 'while'

  if (Lexer.isNot(Token::LParen))
    return Error("left parenthesis expected in while loop");
  Lex();  // eat LParen '('.

  if (parseExpression(Condition))
    return true;

  if (Lexer.isNot(Token::RParen))
    return Error("right parenthesis expected in while loop");
  Lex();  // eat RParen ')'.

  if (parseStatement(Statement))
    return true;

  Res = WhileStatementAST::create(std::move(Condition), std::move(Statement));
  return false;
}

/// \brief Parse an expression statement.
/// exprStatement ::= Expression ";"
bool CMMParser::parseExprStatement(std::unique_ptr<StatementAST> &Res) {
  std::unique_ptr<ExpressionAST> Expression;
  if (parseExpression(Expression))
    return true;
  if (Lexer.isNot(Token::Semicolon))
    return Error("missing semicolon in statement");
  Lex();  // eat the semicolon
  Res.reset(new ExprStatementAST(std::move(Expression)));
  return false;
}

/// \brief Parse a return statement.
/// returnStatement ::= "return" ";"
/// returnStatement ::= "return" Expression ";"
bool CMMParser::parseReturnStatement(std::unique_ptr<StatementAST> &Res) {
  std::unique_ptr<ExpressionAST> ReturnValue;

  assert(Lexer.is(Token::Kw_return) && "parseIfStatement: unknown token");
  Lex();  // eat the 'return'.

  if (Lexer.isNot(Token::Semicolon) && parseExpression(ReturnValue))
    return true;
  if (Lexer.isNot(Token::Semicolon))
    return Error("unexpected token after return value");
  Lex();  // eat the semicolon.
  Res.reset(new ReturnStatementAST(std::move(ReturnValue)));
  return false;
}

/// \brief Parse a break statement.
/// breakStatement ::= "break" ";"
bool CMMParser::parseBreakStatement(std::unique_ptr<StatementAST> &Res) {
  assert(Lexer.is(Token::Kw_break) && "parseIfStatement: unknown token");
  Lex();  // eat the 'break'.
  if (Lexer.isNot(Token::Semicolon))
    return Error("unexpected token after break");
  Lex();  // eat the semicolon.
  Res.reset(new BreakStatementAST);
  return false;
}

/// \brief Parse a continue statement.
/// continueStatement ::= "continue" ";"
bool CMMParser::parseContinueStatement(std::unique_ptr<StatementAST> &Res) {
  assert(Lexer.is(Token::Kw_continue) && "parseIfStatement: unknown token");
  Lex();  // eat the 'continue'.
  if (Lexer.isNot(Token::Semicolon))
    return Error("unexpected token after continue");
  Lex();  // eat the semicolon
  Res.reset(new ContinueStatementAST);
  return false;
}

/// DeclarationStatement ::= TypeSpecifier _DeclarationStatement
bool CMMParser::parseDeclarationStatement(std::unique_ptr<StatementAST> &Res) {
  cvm::BasicType Type;
  if (parseTypeSpecifier(Type))
    return true;
  return parseDeclarationStatement(Type, Res);
}

/// \brief Parse a declaration (auxiliary)
/// _DeclarationStatement ::= SingleDeclaration+
/// SingleDeclaration ::= identifier "=" Expression
/// SingleDeclaration ::= identifier ("[" Expression "]")+
bool CMMParser::parseDeclarationStatement(cvm::BasicType Type,
                                          std::unique_ptr<StatementAST> &Res) {
  auto DeclList = new DeclarationListAST(Type);

  for (;;) {
    if (Lexer.isNot(Token::Identifier))
      return Error("identifier expected");
    std::string Name = Lexer.getStrVal();
    Lex(); // eat the identifier

    std::unique_ptr<ExpressionAST> InitExpr;
    std::list<std::unique_ptr<ExpressionAST>> CountExprList;
    while (Lexer.is(Token::LBrac)) {
      Lex(); // eat the '['
      std::unique_ptr<ExpressionAST> CountExpr;
      if (parseExpression(CountExpr))
        return true;
      if (Lexer.isNot(Token::RBrac))
        return Error("RBrac ']' expected in array declaration");
      Lex(); // eat the ']'
      CountExprList.emplace_back(std::move(CountExpr));
    }
    if (Lexer.is(Token::Equal)) {
      Lex(); // eat the '='
      if (parseExpression(InitExpr))
        return true;
    }

    // Emit
    DeclList->addDeclaration(Name,
                             std::move(InitExpr), std::move(CountExprList));

    if (Lexer.isNot(Token::Comma))
      break;
    Lex(); // Eat the ','
  }
  if (Lexer.isNot(Token::Semicolon))
    return Error("expected semicolon in the declaration");
  Lex(); // Eat the semicolon
  Res.reset(DeclList);
  return false;
}
