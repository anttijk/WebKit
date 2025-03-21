"use strict";

function foo(func, arg) {
    return func(arg);
}

noInline(foo);

function a() { return 1; }
function b() { return 2; }
function c() { return 3; }
function d() { return 4; }
function e() { return 5; }
function f() { return 6; }
function g() { return 7; }
function h() { return 8; }
function i() { return 9; }
function j() { return 0; }
function k() { return 1; }
function l() { return 2; }
function m() { return 3; }

var funcs = [a, b, c, d, e, f, g, h, i, l, m, Array];

for (var i = 0; i < testLoopCount; ++i)
    foo(funcs[i % funcs.length], 1);

var result = null;
try {
    result = foo(Array, -1);
} catch (e) {
    if (e.toString() != "RangeError: Array length must be a positive integer of safe magnitude.")
        throw "Error: bad exception at end: " + e;
}
if (result != null)
    throw "Error: bad result at end: " + result;
