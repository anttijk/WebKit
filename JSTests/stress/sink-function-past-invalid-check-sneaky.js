function foo(p) {
    var o = function() { };
    var q = {f: p ? o : 42};
    var tmp = q.f + 1;
}

noInline(foo);

for (var i = 0; i < testLoopCount; ++i)
    foo(false);

foo(true);

