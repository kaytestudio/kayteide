#pragma once
// InFormWeb.hpp - KayteIDE InForm plugin
// Web applications from Kayte .kjs programs and InForm forms. A .kjs program
// drives its GUI through qt("..."); kayte-web.js (plugins/InForm/web) gives a
// browser the same qt("...") API on the DOM, so the program that runs on the
// desktop through libkayte_qt6 also runs as a web page:
//
//     index.html      the page: forms embedded as JSON, kayte-web.css / .js
//     kayte-web.js    the browser runtime (qt, print, msgbox, ...)
//     kayte-web.css   InForm-style look, light and dark
//     <Program>.js    the .kjs program, translated where .kjs differs from JS
//
// The web form keeps every InForm control (frames, radio buttons, sliders,
// progress bars, pictures, menus) - more than a desktop .kfm can hold.

#include "InFormDocument.hpp"

namespace InForm {

// The form as JSON for kayte-web.js. `kfmFileName` is the name the program
// passes to qt("loadform", ...).
QString webFormJson(const Document &doc, const QString &kfmFileName);

// .kjs -> browser JavaScript. Rewrites what .kjs adds to JavaScript -
// `for i from A to B [step S]`, `call f()`, function-wide `let` / `const` -
// and reports what a browser cannot do the same way (blocking qt("wait"),
// process(), whole-number `/`).
struct KjsTranslation
{
    QString     js;
    QStringList warnings;
};
KjsTranslation translateKjsForBrowser(const QString &kjs);

// kayte-web.js / kayte-web.css, as bundled in the plugin.
QString webRuntimeScript();
QString webRuntimeStyle();

// The page that runs `programScript` (a .js file next to it) with the form.
QString webPage(const Document &doc, const QString &kfmFileName,
                const QString &programScript, const QString &title);

struct WebExport
{
    QString     indexPath;  // the page to open
    QStringList files;      // everything written
    QStringList warnings;
};

// Writes the web app for the .kjs program `kjsPath` into `outDir`. Images the
// form uses are copied from `assetDir` (the folder of the form's files).
bool exportWebApp(const Document &doc, const QString &kjsPath, const QString &kfmFileName,
                  const QString &outDir, const QString &assetDir,
                  WebExport *result, QString *error = nullptr);

} // namespace InForm
