// The synthesized constructor's lexical-scope allocation can fail.
// Firefox78 Parser.cpp checks finishLexicalScope before appending the member.
if (typeof oomTest === 'function') {
    oomTest(() => {
        let result = evaluate('(function(){class C { x=42; } return new C().x;})();');
        assertEq(result,42);
    });
}
