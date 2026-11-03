/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Updating options for the same path must not reset the editor. WebIDE's
// AppManagerRenderer (removed in 197f4cbaa4) supplied the app status/icon UI;
// standalone ProjectEditor retains the options and uses a normal root label.
add_task(function* () {
  let path = buildTempDirectoryStructure();
  let projecteditor = yield addProjectEditorTab();
  let options = {
    name: "Test",
    iconUrl: "chrome://devtools/skin/images/tool-options.svg",
    projectOverviewURL: SAMPLE_WEBAPP_URL
  };
  yield projecteditor.setProjectToAppPath(path, options);

  let project = projecteditor.project;
  let store = project.localStores.get(path);
  let root = store.root;
  projecteditor.openResource(root);
  let editor = projecteditor.editorFor(root);
  yield editor.appended;
  let label = projecteditor.projectTree.getViewContainer(root).label;
  is(project.appManagerOpts, options, "The initial options are retained");
  is(label.textContent, root.displayName, "The directory has its normal label");
  ok(!editor.isEditable && editor.isClean(), "The directory view is read-only and clean");

  function failIfResourceAdded() {
    ok(false, "Updating options must not add resources");
  }
  project.on("resource-added", failIfResourceAdded);
  try {
    for (let [index, status] of ["error", "warning", "valid"].entries()) {
      options = {
        name: "Test" + (index + 2),
        iconUrl: "chrome://devtools/skin/images/tool-inspector.svg",
        projectOverviewURL: SAMPLE_WEBAPP_URL,
        validationStatus: status
      };
      yield projecteditor.setProjectToAppPath(path, options);
      is(projecteditor.project, project, "The project is not replaced");
      is(project.appManagerOpts, options, "The new options replace the previous options");
      is(project.appManagerOpts.validationStatus, status, "The updated status is retained");
      is(project.localStores.get(path), store, "The existing store is retained");
      is(store.root, root, "The existing root resource is retained");
      is(projecteditor.editorFor(root), editor, "The open directory editor is retained");
      is(projecteditor.projectTree.getViewContainer(root).label, label,
         "The existing label element is retained");
      is(label.textContent, root.displayName, "Updating app options preserves the directory label");
      ok(!editor.isEditable && editor.isClean(), "Updating options does not dirty the directory");
    }
  } finally {
    project.off("resource-added", failIfResourceAdded);
  }
});
