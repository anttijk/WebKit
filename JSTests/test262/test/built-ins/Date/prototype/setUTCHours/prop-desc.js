// Copyright (C) 2024 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-date.prototype.setutchours
description: >
  Property descriptor for Date.prototype.setUTCHours.
info: |
  Date.prototype.setUTCHours ( hour [ , min [ , sec [ , ms ] ] ] )

  17 ECMAScript Standard Built-in Objects:
    Every other data property described in clauses 19 through 28 and in
    Annex B.2 has the attributes { [[Writable]]: true, [[Enumerable]]: false,
    [[Configurable]]: true } unless otherwise specified.
includes: [propertyHelper.js]
---*/

verifyProperty(Date.prototype, "setUTCHours", {
  writable: true,
  enumerable: false,
  configurable: true,
});
