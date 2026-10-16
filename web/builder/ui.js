// SPDX-License-Identifier: GPL-3.0-only
// The standalone builder page: mounts the builder module (builder.js) in #app and shows it. The tests reach the page's
// state through window.optimistBuilder (the module's handle). docs/BUILDER-WEB.md.
import { mountBuilder } from "./builder.js";

const handle = mountBuilder(document.getElementById("app"), {});
window.optimistBuilder = handle;
handle.activate();
