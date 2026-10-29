#!/usr/bin/env python3
"""Package this folder into the .vsix the editor installs - without node.

`npx @vscode/vsce package` is the canonical way and stays documented, but it
needs node, npm and a network fetch. This produces the same archive with the
Python standard library alone, which matters because the committed .vsix is the
one thing users actually get: a source change nobody repackaged is invisible to
them, and that has now happened twice (the VU language shipped in 0.3.0 sources
against a 0.2.0 package, and menu stylesheets did the same).

    python3 tools/vscode-tyrax/package-vsix.py [--version X.Y.Z]

Writes <name>-<version>.vsix (tyrax-X.Y.Z.vsix) next to this script, removes
any older .vsix (the editor installs the newest, but a stale one is still a
trap), and prints what went in. The same file is what gets published to the
Visual Studio Marketplace and Open VSX (`vsce publish --packagePath`,
`ovsx publish`; see README.md, "Publishing").

The extension was packaged as `tyrax.tyrax-flownode` until 0.4.x, which was
never published anywhere; 0.5.0 is `tyrax.tyrax` and the editor uninstalls the
old id when it installs this one.
"""

import argparse
import json
import pathlib
import sys
import zipfile

HERE = pathlib.Path(__file__).resolve().parent

# Everything vsce would pick up, in the layout it produces. README/CHANGELOG are
# lower-cased in the package because the manifest's asset paths say so.
FILES = [
    ("package.json", "extension/package.json"),
    ("language-configuration.json", "extension/language-configuration.json"),
    ("vu-language-configuration.json", "extension/vu-language-configuration.json"),
    ("icon.png", "extension/icon.png"),
    ("README.md", "extension/readme.md"),
    ("CHANGELOG.md", "extension/changelog.md"),
    ("../../LICENSE", "extension/LICENSE.txt"),
]
# Every module, globbed rather than listed: 0.4.x listed extension.js and vu.js
# by hand while extension.js also required vuscript.js and now debug.js - a
# module missing from the package is an extension that fails to activate.
JS = sorted(p.name for p in HERE.glob("*.js"))

CONTENT_TYPES = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
    '<Default Extension=".js" ContentType="application/javascript"/>'
    '<Default Extension=".json" ContentType="application/json"/>'
    '<Default Extension=".md" ContentType="text/markdown"/>'
    '<Default Extension=".png" ContentType="image/png"/>'
    '<Default Extension=".vsixmanifest" ContentType="text/xml"/>'
    "</Types>"
)

REPO = "https://github.com/doctorspider42/tyraX"


def xml_escape(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
             .replace('"', "&quot;"))


def manifest(pkg):
    tags = list(pkg.get("keywords", []))
    for lang in pkg["contributes"]["languages"]:
        tags.append(lang["id"])
        for ext in lang.get("extensions", []):
            tags.append("__ext_" + ext.lstrip("."))
    return f"""<?xml version="1.0" encoding="utf-8"?>
\t<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
\t\t<Metadata>
\t\t\t<Identity Language="en-US" Id="{pkg['name']}" Version="{pkg['version']}" Publisher="{pkg['publisher']}" />
\t\t\t<DisplayName>{xml_escape(pkg['displayName'])}</DisplayName>
\t\t\t<Description xml:space="preserve">{xml_escape(pkg['description'])}</Description>
\t\t\t<Tags>{xml_escape(','.join(dict.fromkeys(tags)))}</Tags>
\t\t\t<Categories>{xml_escape(','.join(pkg.get('categories', [])))}</Categories>
\t\t\t<GalleryFlags>Public</GalleryFlags>
\t\t\t<Properties>
\t\t\t\t<Property Id="Microsoft.VisualStudio.Code.Engine" Value="{pkg['engines']['vscode']}" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="{','.join(pkg.get('extensionDependencies', []))}" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Code.LocalizedLanguages" Value="" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Code.EnabledApiProposals" Value="" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Code.ExecutesCode" Value="true" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Services.Links.Source" Value="{REPO}.git" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Services.Links.Getstarted" Value="{REPO}.git" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Services.Links.GitHub" Value="{REPO}.git" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Services.Links.Support" Value="{REPO}/issues" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Services.Links.Learn" Value="{REPO}#readme" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Services.GitHubFlavoredMarkdown" Value="true" />
\t\t\t\t<Property Id="Microsoft.VisualStudio.Services.Content.Pricing" Value="Free"/>
\t\t\t</Properties>
\t\t\t<Icon>extension/icon.png</Icon>
\t\t</Metadata>
\t\t<Installation>
\t\t\t<InstallationTarget Id="Microsoft.VisualStudio.Code"/>
\t\t</Installation>
\t\t<Dependencies/>
\t\t<Assets>
\t\t\t<Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
\t\t\t<Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/readme.md" Addressable="true" />
\t\t\t<Asset Type="Microsoft.VisualStudio.Services.Content.Changelog" Path="extension/changelog.md" Addressable="true" />
\t\t\t<Asset Type="Microsoft.VisualStudio.Services.Content.License" Path="extension/LICENSE.txt" Addressable="true" />
\t\t\t<Asset Type="Microsoft.VisualStudio.Services.Icons.Default" Path="extension/icon.png" Addressable="true" />
\t\t</Assets>
\t</PackageManifest>"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", help="override package.json's version")
    args = ap.parse_args()

    pkg = json.loads((HERE / "package.json").read_text(encoding="utf-8"))
    if args.version:
        pkg["version"] = args.version
        (HERE / "package.json").write_text(
            json.dumps(pkg, indent=2) + "\n", encoding="utf-8")

    entries = list(FILES) + [(js, "extension/" + js) for js in JS]
    # Every grammar and snippet file the manifest declares, taken from the
    # manifest rather than a second list here - a language added to package.json
    # and forgotten in this script is the exact failure this file exists to stop.
    for section, key in (("grammars", "path"), ("snippets", "path")):
        for item in pkg["contributes"].get(section, []):
            rel = item[key].lstrip("./")
            entries.append((rel, "extension/" + rel))

    missing = [src for src, _ in entries if not (HERE / src).exists()]
    if missing:
        sys.exit("missing file(s): " + ", ".join(missing))

    out = HERE / f"{pkg['name']}-{pkg['version']}.vsix"
    for old in HERE.glob("*.vsix"):
        if old != out:
            old.unlink()
            print(f"removed stale {old.name}")

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("extension.vsixmanifest", manifest(pkg))
        z.writestr("[Content_Types].xml", CONTENT_TYPES)
        for src, dst in entries:
            z.write(HERE / src, dst)
            print(f"  + {dst}")
    print(f"wrote {out.name} ({out.stat().st_size} bytes)")
    print("languages: " +
          ", ".join(l["id"] + " (" + " ".join(l.get("extensions", [])) + ")"
                    for l in pkg["contributes"]["languages"]))


if __name__ == "__main__":
    main()
