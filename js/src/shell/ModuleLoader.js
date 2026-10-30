/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A basic synchronous module loader for testing the shell.

const ReflectLoader = new class {
    constructor() {
        this.registry = new Map();
        this.modulePaths = new WeakMap();
        this.loadPath = getModuleLoadPath();
    }

    resolve(name, referencingInfo) {
        if (os.path.isAbsolute(name))
            return this.normalize(name);

        // os.path in this shell provides isAbsolute/join, but no dirname.
        // Bare names retain the shell's --module-load-path convention. Only
        // explicit relative names resolve against the importing module.
        name = name.replace(/\\/g, "/");
        let relative = name.startsWith("./") || name.startsWith("../");
        let path = relative && typeof referencingInfo === "string"
                   ? referencingInfo.replace(/\\/g, "/") : "";
        let base = path ? path.slice(0, path.lastIndexOf("/") + 1) : this.loadPath;
        return this.normalize(os.path.join(base, name));
    }

    normalize(name) {
        let path = name.replace(/\\/g, "/");
        let prefix = "";
        // This file passes through the C preprocessor; avoid a regexp ending
        // in escaped slash, whose two slash tokens become a C++ comment.
        if (path.length >= 3 && /^[A-Za-z]$/.test(path[0]) &&
            path[1] === ":" && path[2] === "/") {
            prefix = path.slice(0, 2).toUpperCase() + "/";
            path = path.slice(3);
        } else if (path.startsWith("//")) {
            prefix = "//";
            path = path.slice(2);
        } else if (path.startsWith("/")) {
            prefix = "/";
            path = path.slice(1);
        }
        let parts = [];
        for (let part of path.split("/")) {
            if (!part || part === ".")
                continue;
            if (part === ".." && parts.length && parts[parts.length - 1] !== ".." &&
                (prefix !== "//" || parts.length > 2))
                parts.pop();
            else if (part !== ".." || !prefix)
                parts.push(part);
        }
        return prefix + parts.join("/");
    }

    fetch(path) {
        return os.file.readFile(path);
    }

    loadAndParse(name) {
        let path = this.resolve(name);

        if (this.registry.has(path))
            return this.registry.get(path);

        let source = this.fetch(path);
        let module = parseModule(source, path);
        setModulePrivate(module, path);
        this.modulePaths.set(module, path);
        this.registry.set(path, module);
        return module;
    }

    ["import"](name, referencingInfo) {
        let module = this.loadAndParse(this.resolve(name, referencingInfo));
        module.declarationInstantiation();
        return module.evaluation();
    }

    loadAndExecute(path) {
        let module = this.loadAndParse(path);
        module.declarationInstantiation();
        return module.evaluation();
    }

    populateImportMeta(module, metaObject) {
        // For the shell, use the script's filename as the base URL.

        let path;
        if (this.modulePaths.has(module)) {
            path = this.modulePaths.get(module);
        } else {
            path = "(unknown)";
        }
        metaObject.url = path;
    }
};

Object.defineProperty(Reflect, "Loader", {
    value: ReflectLoader, writable: true, configurable: true
});

setModuleResolveHook((referencingInfo, requestName) => {
    let path = ReflectLoader.resolve(requestName, referencingInfo);
    return ReflectLoader.loadAndParse(path);
});
 
setModuleMetadataHook((module, metaObject) => {
    ReflectLoader.populateImportMeta(module, metaObject);
});
 
setModuleDynamicImportHook((referencingInfo, specifier, promise) => {
    try {
        let path = ReflectLoader.resolve(specifier, referencingInfo);
        let evaluation = ReflectLoader.loadAndExecute(path);
        if (evaluation && typeof evaluation.then === "function") {
            evaluation.then(
                () => finishDynamicModuleImport(referencingInfo, specifier, promise),
                err => abortDynamicModuleImport(referencingInfo, specifier, promise, err));
        } else {
            finishDynamicModuleImport(referencingInfo, specifier, promise);
        }
    } catch (err) {
        abortDynamicModuleImport(referencingInfo, specifier, promise, err);
    }
});
