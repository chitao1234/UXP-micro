/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/tests.h"

#include "jsfun.h"

BEGIN_TEST(testFunctionProperties)
{
    JS::RootedValue x(cx);
    EVAL("(function f() {})", &x);

    JS::RootedObject obj(cx, x.toObjectOrNull());

    JS::RootedValue y(cx);
    CHECK(JS_GetProperty(cx, obj, "arguments", &y));
    CHECK(y.isNull());

    CHECK(JS_GetProperty(cx, obj, "caller", &y));
    CHECK(y.isNull());

    return true;
}
END_TEST(testFunctionProperties)

BEGIN_TEST(testFunctionPrototypeNativeBootstrap)
{
    // Repeat standard-class bootstrap in fresh globals, retaining the native
    // prototype through GC before using its script, call, and reflection APIs.
    for (unsigned i = 0; i < 8; i++) {
        JS::RootedObject fresh(cx, createGlobal());
        CHECK(fresh);
        JSAutoCompartment ac(cx, fresh);

        JS::RootedValue value(cx);
        EVAL("Function.prototype", &value);
        JS::RootedFunction fun(cx, JS_ValueToFunction(cx, value));
        CHECK(fun);
        CHECK(fun->isNative());
        CHECK(!JS_IsConstructor(fun));
        CHECK_EQUAL(JS_GetFunctionArity(fun), uint16_t(0));
        CHECK_NULL(JS_GetFunctionScript(cx, fun));
        CHECK(!JS_IsExceptionPending(cx));

        JS_GC(cx);

        JS::RootedValue result(cx);
        CHECK(JS_CallFunctionValue(cx, fresh, value, JS::HandleValueArray::empty(), &result));
        CHECK(result.isUndefined());

        JS::RootedObject constructed(cx);
        CHECK(!JS::Construct(cx, value, JS::HandleValueArray::empty(), &constructed));
        CHECK(JS_IsExceptionPending(cx));
        JS_ClearPendingException(cx);

        JS::RootedObject prototype(cx, JS_GetFunctionObject(fun));
        CHECK(!JS_GetProperty(cx, prototype, "caller", &result));
        CHECK(JS_IsExceptionPending(cx));
        JS_ClearPendingException(cx);
        CHECK(!JS_GetProperty(cx, prototype, "arguments", &result));
        CHECK(JS_IsExceptionPending(cx));
        JS_ClearPendingException(cx);

        EVAL("Object.getPrototypeOf(Function.prototype) === Object.prototype && "
             "Function.prototype.name === '' && Function.prototype.length === 0 && "
             "!Object.prototype.hasOwnProperty.call(Function.prototype, 'prototype')",
             &result);
        CHECK(result.isBoolean() && result.toBoolean());
    }
    return true;
}
END_TEST(testFunctionPrototypeNativeBootstrap)
