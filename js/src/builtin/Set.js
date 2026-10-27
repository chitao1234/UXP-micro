/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// ES2017 draft rev 0e10c9f29fca1385980c08a7d5e7bb3eb775e2e4
// 23.2.1.1 Set, steps 6-8
function SetConstructorInit(iterable) {
    var set = this;

    // Step 6.a.
    var adder = set.add;

    // Step 6.b.
    if (!IsCallable(adder))
        ThrowTypeError(JSMSG_NOT_FUNCTION, typeof adder);

    // Steps 6.c-8.
    for (var nextValue of allowContentIter(iterable))
        callContentFunction(adder, set, nextValue);
}

/* ES6 20121122 draft 15.16.4.6. */
function SetForEach(callbackfn, thisArg = undefined) {
    /* Step 1-2. */
    var S = this;
    if (!IsObject(S))
        ThrowTypeError(JSMSG_INCOMPATIBLE_PROTO, "Set", "forEach", typeof S);

    /* Step 3-4. */
    try {
        callFunction(std_Set_has, S);
    } catch (e) {
        // has will throw on non-Set objects, throw our own error in that case.
        ThrowTypeError(JSMSG_INCOMPATIBLE_PROTO, "Set", "forEach", typeof S);
    }

    /* Step 5-6. */
    if (!IsCallable(callbackfn))
        ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, callbackfn));

    /* Step 7-8. */
    var values = callFunction(std_Set_iterator, S);
    while (true) {
        var result = callFunction(SetIteratorNext, values);
        if (result.done)
            break;
        var value = result.value;
        callContentFunction(callbackfn, thisArg, value, value, S);
    }
}

// ES6 final draft 23.2.2.2.
function SetSpecies() {
    // Step 1.
    return this;
}
_SetCanonicalName(SetSpecies, "get [Symbol.species]");


var setIteratorTemp = { setIterationResult : null };

function SetIteratorNext() {
    // Step 1.
    var O = this;

    // Steps 2-3.
    if (!IsObject(O) || (O = GuardToSetIterator(O)) === null)
        return callFunction(CallSetIteratorMethodIfWrapped, this, "SetIteratorNext");

    // Steps 4-5 (implemented in _GetNextSetEntryForIterator).
    // Steps 8-9 (omitted).

    var setIterationResult = setIteratorTemp.setIterationResult;
    if (!setIterationResult) {
        setIterationResult = setIteratorTemp.setIterationResult = _CreateSetIterationResult();
    }

    var retVal = {value: undefined, done: true};

    // Steps 10.a, 11.
    var done = _GetNextSetEntryForIterator(O, setIterationResult);
    if (!done) {
        // Steps 10.b-c (omitted).

        // Step 6.
        var itemKind = UnsafeGetInt32FromReservedSlot(O, ITERATOR_SLOT_ITEM_KIND);

        var result;
        if (itemKind === ITEM_KIND_VALUE) {
            // Step 10.d.i.
            result = setIterationResult[0];
        } else {
            // Step 10.d.ii.
            assert(itemKind === ITEM_KIND_KEY_AND_VALUE, itemKind);
            result = [setIterationResult[0], setIterationResult[0]];
        }

        setIterationResult[0] = null;
        retVal.value = result;
        retVal.done = false;
    }

    // Steps 7, 10.d, 12.
    return retVal;
}

// ECMA-262 Set composition methods. Keep the set-like protocol separate from
// the receiver's internal Set data: only other.size/has/keys are observable.
// Captured native Set operations validate/unwrap receivers for individual
// accesses, keeping these algorithms and newly created Sets in the method's
// realm instead of rerunning the whole algorithm in a wrapped receiver's realm.
function GetSetRecord(obj) {
    if (!IsObject(obj))
        ThrowTypeError(JSMSG_NOT_NONNULL_OBJECT, "set-like argument");

    var rawSize = obj.size;
    var numSize = +rawSize;
    if (numSize !== numSize)
        ThrowTypeError(JSMSG_UNEXPECTED_TYPE, "size", "NaN");
    var size = ToInteger(numSize);
    if (size < 0)
        ThrowRangeError(JSMSG_SET_NEGATIVE_SIZE);

    var has = obj.has;
    if (!IsCallable(has))
        ThrowTypeError(JSMSG_NOT_FUNCTION, typeof has);
    var keys = obj.keys;
    if (!IsCallable(keys))
        ThrowTypeError(JSMSG_NOT_FUNCTION, typeof keys);
    return {set: obj, size, has, keys};
}

// GetIteratorFromMethod followed by GetIteratorDirect. Snapshot next exactly
// once, even when keys/next accessors mutate the receiver or the iterator.
function SetMethodsGetIterator(rec) {
    var iterator = callContentFunction(rec.keys, rec.set);
    if (!IsObject(iterator))
        ThrowTypeError(JSMSG_NOT_NONNULL_OBJECT, "set-like keys result");
    return {iterator, next: iterator.next, value: undefined};
}

function SetMethodsIteratorStep(rec) {
    var next = callContentFunction(rec.next, rec.iterator);
    if (!IsObject(next))
        ThrowTypeError(JSMSG_NOT_NONNULL_OBJECT, "iterator result");
    // Snapshot value once, in spec order. Reuse this private record rather
    // than allocating a second result object for every user iterator step.
    if (next.done)
        return false;
    rec.value = next.value;
    return true;
}

function SetMethodsIteratorClose(iterator) {
    var returnMethod = GetMethod(iterator, "return");
    if (returnMethod !== undefined) {
        var innerResult = callContentFunction(returnMethod, iterator);
        if (!IsObject(innerResult))
            ThrowTypeError(JSMSG_NOT_NONNULL_OBJECT, "iterator return result");
    }
}

function SetCopy(set) {
    // Intrinsics bypass a changed constructor, @@species, add, values, and
    // @@iterator. New sets always use the method realm's %Set.prototype%.
    var result = std_Set_create();
    var values = callFunction(std_Set_iterator, set);
    while (true) {
        var next = callFunction(SetIteratorNext, values);
        if (next.done)
            return result;
        callFunction(std_Set_add, result, next.value);
    }
}

function SetUnion(other) {
    var O = this;
    callFunction(std_Set_size, O); // Require [[SetData]] before touching other.

    var otherRec = GetSetRecord(other);
    var keys = SetMethodsGetIterator(otherRec);
    var result = SetCopy(O);
    while (true) {
        if (!SetMethodsIteratorStep(keys))
            return result;
        callFunction(std_Set_add, result, keys.value);
    }
}

function SetIntersection(other) {
    var O = this;
    callFunction(std_Set_size, O); // Require [[SetData]] before touching other.

    var otherRec = GetSetRecord(other);
    var result = std_Set_create();
    if (callFunction(std_Set_size, O) <= otherRec.size) {
        var values = callFunction(std_Set_iterator, O);
        while (true) {
            var next = callFunction(SetIteratorNext, values);
            if (next.done)
                return result;
            var value = next.value;
            if (callContentFunction(otherRec.has, otherRec.set, value))
                callFunction(std_Set_add, result, value);
        }
    }

    var keys = SetMethodsGetIterator(otherRec);
    while (true) {
        if (!SetMethodsIteratorStep(keys))
            return result;
        if (callFunction(std_Set_has, O, keys.value))
            callFunction(std_Set_add, result, keys.value);
    }
}

function SetDifference(other) {
    var O = this;
    callFunction(std_Set_size, O); // Require [[SetData]] before touching other.

    var otherRec = GetSetRecord(other);
    var result = SetCopy(O);
    if (callFunction(std_Set_size, O) <= otherRec.size) {
        // Iterate the copied data: other.has can mutate O without changing
        // the list that difference is required to visit.
        var values = callFunction(std_Set_iterator, result);
        while (true) {
            var next = callFunction(SetIteratorNext, values);
            if (next.done)
                return result;
            if (callContentFunction(otherRec.has, otherRec.set, next.value))
                callFunction(std_Set_delete, result, next.value);
        }
    }

    var keys = SetMethodsGetIterator(otherRec);
    while (true) {
        if (!SetMethodsIteratorStep(keys))
            return result;
        callFunction(std_Set_delete, result, keys.value);
    }
}

function SetSymmetricDifference(other) {
    var O = this;
    callFunction(std_Set_size, O); // Require [[SetData]] before touching other.

    var otherRec = GetSetRecord(other);
    var keys = SetMethodsGetIterator(otherRec);
    var result = SetCopy(O);
    while (true) {
        if (!SetMethodsIteratorStep(keys))
            return result;
        // Membership uses O, not the changing result: duplicate keys from a
        // set-like iterator must not toggle an entry repeatedly.
        if (callFunction(std_Set_has, O, keys.value))
            callFunction(std_Set_delete, result, keys.value);
        else
            callFunction(std_Set_add, result, keys.value);
    }
}

function SetIsSubsetOf(other) {
    var O = this;
    callFunction(std_Set_size, O); // Require [[SetData]] before touching other.

    var otherRec = GetSetRecord(other);
    if (callFunction(std_Set_size, O) > otherRec.size)
        return false;
    var values = callFunction(std_Set_iterator, O);
    while (true) {
        var next = callFunction(SetIteratorNext, values);
        if (next.done)
            return true;
        if (!callContentFunction(otherRec.has, otherRec.set, next.value))
            return false;
    }
}

function SetIsSupersetOf(other) {
    var O = this;
    callFunction(std_Set_size, O); // Require [[SetData]] before touching other.

    var otherRec = GetSetRecord(other);
    if (callFunction(std_Set_size, O) < otherRec.size)
        return false;
    var keys = SetMethodsGetIterator(otherRec);
    while (true) {
        if (!SetMethodsIteratorStep(keys))
            return true;
        if (!callFunction(std_Set_has, O, keys.value)) {
            SetMethodsIteratorClose(keys.iterator);
            return false;
        }
    }
}

function SetIsDisjointFrom(other) {
    var O = this;
    callFunction(std_Set_size, O); // Require [[SetData]] before touching other.

    var otherRec = GetSetRecord(other);
    if (callFunction(std_Set_size, O) <= otherRec.size) {
        var values = callFunction(std_Set_iterator, O);
        while (true) {
            var next = callFunction(SetIteratorNext, values);
            if (next.done)
                return true;
            if (callContentFunction(otherRec.has, otherRec.set, next.value))
                return false;
        }
    }

    var keys = SetMethodsGetIterator(otherRec);
    while (true) {
        if (!SetMethodsIteratorStep(keys))
            return true;
        if (callFunction(std_Set_has, O, keys.value)) {
            SetMethodsIteratorClose(keys.iterator);
            return false;
        }
    }
}
