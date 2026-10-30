import {value as base} from "./dependency.js";
export const value = await Promise.resolve(base + 1);
export const url = import.meta.url;
