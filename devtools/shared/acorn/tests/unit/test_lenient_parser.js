/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test that acorn's lenient parser gives something usable.
 */

const acorn_loose = require("acorn/acorn_loose");

function run_test() {
  // The recovery oracle below uses ES5, where `let` is an identifier. Keep
  // that grammar explicit; the parser's default is now ES6.
  let actualAST = acorn_loose.parse_dammit("let x = 10", {ecmaVersion: 5});

  do_print("Actual AST:");
  do_print(JSON.stringify(actualAST, null, 2));
  do_print("Expected AST:");
  do_print(JSON.stringify(expectedAST, null, 2));

  checkEquivalentASTs(expectedAST, actualAST);

  // Under the default ES6 grammar this input is valid and must remain a
  // lexical declaration, rather than being split into recovery statements.
  let defaultAST = acorn_loose.parse_dammit("let x = 10", {});
  do_check_eq(defaultAST.body.length, 1);
  let declaration = defaultAST.body[0];
  do_check_eq(declaration.type, "VariableDeclaration");
  do_check_eq(declaration.kind, "let");
  do_check_eq(declaration.declarations.length, 1);
  let declarator = declaration.declarations[0];
  do_check_eq(declarator.type, "VariableDeclarator");
  do_check_eq(declarator.id.type, "Identifier");
  do_check_eq(declarator.id.name, "x");
  do_check_eq(declarator.init.type, "Literal");
  do_check_eq(declarator.init.value, 10);
}

const expectedAST = {
  "type": "Program",
  "start": 0,
  "end": 10,
  "body": [
    {
      "type": "ExpressionStatement",
      "start": 0,
      "end": 3,
      "expression": {
        "type": "Identifier",
        "start": 0,
        "end": 3,
        "name": "let"
      }
    },
    {
      "type": "ExpressionStatement",
      "start": 4,
      "end": 10,
      "expression": {
        "type": "AssignmentExpression",
        "start": 4,
        "end": 10,
        "operator": "=",
        "left": {
          "type": "Identifier",
          "start": 4,
          "end": 5,
          "name": "x"
        },
        "right": {
          "type": "Literal",
          "start": 8,
          "end": 10,
          "value": 10,
          "raw": "10"
        }
      }
    }
  ]
};
