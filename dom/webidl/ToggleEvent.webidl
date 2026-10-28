/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

[Constructor(DOMString type, optional ToggleEventInit eventInitDict), Exposed=Window]
interface ToggleEvent : Event {
  readonly attribute DOMString oldState;
  readonly attribute DOMString newState;
  readonly attribute Element? source;
};

dictionary ToggleEventInit : EventInit {
  Element? source = null;
  DOMString oldState = "";
  DOMString newState = "";
};
