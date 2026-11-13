export const value = 42;
export const identity = {};
export const resolve = import.meta.resolve;
export const url = import.meta.url;
export function load(specifier) { return import(specifier); }
window.moduleEvaluations = (window.moduleEvaluations || 0) + 1;
