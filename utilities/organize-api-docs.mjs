#!/usr/bin/env node

import fs from "fs/promises";
import path from "path";
import { XMLParser } from "fast-xml-parser";

/// Original Doxygen XML output
const XML_DIR = "docs/api/xml";
/// The folder where the moxygen markdown files were output.
const MD_DIR = "docs/api/md";
/// Moxygen had problems with concepts, so we regenerate from XML at end
const CONCEPTS_OUT_DIR = "docs/api/md/Concepts";

// -----------------------------------------------------------------------------
// Sub-divide the markdown files output by moxygen into appropriate sub-folders
// for easier navigation.
// The YAML front-matter contains a `kind` field which is used to categorize.
// -----------------------------------------------------------------------------
const KIND_DIR = {
    class: "Classes",
    struct: "Classes",
    interface: "Classes",
    union: "Classes",

    namespace: "Namespaces",
    file: "Files",
    dir: "Files",

    group: "Groups",
    concept: "Concepts",
    page: "Pages",
    example: "Examples",
};

// -----------------------------------------------------------------------------
// YAML frontmatter parsing routine.
// -----------------------------------------------------------------------------

function parseFrontmatter(text) {
    const m = text.match(/^---\n([\s\S]*?)\n---/);
    if (!m) {
        return null;
    }

    const fm = {};

    for (const line of m[1].split("\n")) {
        const i = line.indexOf(":");
        if (i < 0) {
            continue;
        }

        const key = line.slice(0, i).trim();
        let val = line.slice(i + 1).trim();

        val = val.replace(/^"|"$/g, "");

        fm[key] = val;
    }

    return fm;
}

// -----------------------------------------------------------------------------
// Await the directory creation.
// -----------------------------------------------------------------------------

async function ensure(dir) {
    await fs.mkdir(dir, { recursive: true });
}

// -----------------------------------------------------------------------------
// Return an empty string if null.
// -----------------------------------------------------------------------------

function safeStr(v) {
    return (v ?? "").toString();
}

// -----------------------------------------------------------------------------
// File loading routine -- parses the markdown's YAML frontmatter.
// -----------------------------------------------------------------------------
async function loadFiles() {
    const files = await fs.readdir(MD_DIR);

    const entries = [];

    for (const f of files) {
        if (!f.endsWith(".md") || f === "index.md") {
            continue;
        }

        const full = path.join(MD_DIR, f);
        const text = await fs.readFile(full, "utf8");

        const fm = parseFrontmatter(text);
        if (!fm) {
            continue;
        }

        entries.push({
            file: f,
            kind: fm.kind,
            namespace: fm.namespace || "global",
            title: fm.title || f.replace(".md", ""),
        });
    }

    return entries;
}

// -----------------------------------------------------------------------------
// Load all markdown files and determine their subfolder organization
// -----------------------------------------------------------------------------

const files = await loadFiles();

// group by folder + namespace
const tree = {};

for (const f of files) {
    const folder = KIND_DIR[f.kind];
    if (!folder) {
        continue;
    }

    if (!tree[folder]) {
        tree[folder] = {};
    }

    if (!tree[folder][f.namespace]) {
        tree[folder][f.namespace] = [];
    }

    tree[folder][f.namespace].push(f);
}

// -----------------------------------------------------------------------------
// After determining sub-folder organization, move each entity's documentation
// markdown into the determined sub-folder.
// -----------------------------------------------------------------------------
for (const [folder, namespaces] of Object.entries(tree)) {
    await ensure(path.join(MD_DIR, folder));

    for (const nsItems of Object.values(namespaces)) {
        for (const item of nsItems) {
            const src = path.join(MD_DIR, item.file);
            const dst = path.join(MD_DIR, folder, item.file);

            try {
                await fs.rename(src, dst);
            } catch {}
        }
    }
}

// -----------------------------------------------------------------------------
// Create index.md files with links to each entity's documentation markdown to
// place in each newly created sub-folder.
// -----------------------------------------------------------------------------
for (const [folder, namespaces] of Object.entries(tree)) {
    await ensure(path.join(MD_DIR, folder));

    let md = `---
title: ${folder}
---

# ${folder}

`;

    const nsNames = Object.keys(namespaces).sort((a, b) =>
        safeStr(a).localeCompare(safeStr(b))
    );

    for (const ns of nsNames) {
        md += `## ${ns}\n\n`;

        const items = namespaces[ns].sort((a, b) =>
            safeStr(a.title).localeCompare(safeStr(b.title))
        );

        for (const item of items) {
            // The routes for astro-starlight use a purely lower-case normalization
            const base = item.file.replace(/\.md$/, "").toLowerCase();
            md += `- [${item.title}](./${base})\n`;
        }

        md += "\n";
    }

    await fs.writeFile(
        path.join(MD_DIR, folder, "index.md"),
        md
    );
}

// -----------------------------------------------------------------------------
// Setup XML parser for the final pass to generate `concepts` pages.
// -----------------------------------------------------------------------------

const parser = new XMLParser({
    ignoreAttributes: false,
    attributeNamePrefix: "",
});

async function getConceptFiles() {
    const files = await fs.readdir(XML_DIR);
    return files.filter((f) => f.startsWith("concept") && f.endsWith(".xml"));
}

// -----------------------------------------------------------------------------
// Parse and extract the concepts information.
// -----------------------------------------------------------------------------

function parseConcept(xml) {
    const data = parser.parse(xml);

    const compound = data.doxygen?.compounddef || data.compounddef;
    if (!compound) return null;

    const name = compound.compoundname || "UnknownConcept";

    const brief = compound.briefdescription?.para
          ? flatten(compound.briefdescription.para)
          : "";

    const defn = compound.initializer ? compound.initializer['#text'] : "";

    const detailed = compound.detaileddescription?.para
          ? flatten(compound.detaileddescription.para)
          : "";

    return {
        name,
        brief,
        defn,
        detailed,
    };
}

/// Turn the XML map into a single string.
function flatten(p) {
    if (!p) {
        return "";
    }
    if (typeof p === "string") {
        return p;
    }
    if (Array.isArray(p)) {
        return p.map(flatten).join(" ");
    }
    if (typeof p === "object") {
        return Object.values(p).map(flatten).join(" ");
    }
    return "";
}


// -----------------------------------------------------------------------------
// Extract slug for naming
// -----------------------------------------------------------------------------
function slug(name) {
    return name
        .replace(/::/g, "-")
        .replace(/[^a-zA-Z0-9\-]/g, "-")
        .replace(/-+/g, "-")
        .replace(/^-|-$/g, "")
        .toLowerCase();
}

// -----------------------------------------------------------------------------
// Run the concept pages generation and formatting.
// -----------------------------------------------------------------------------

async function main() {
    await ensure(CONCEPTS_OUT_DIR);

    const files = await getConceptFiles();

    const index = [];

    for (const file of files) {
        const xml = await fs.readFile(path.join(XML_DIR, file), "utf8");
        const concept = parseConcept(xml);

        if (!concept) {
            continue;
        }

        const s = slug(concept.name);
        const outFile = `${s}.md`;

        const md = `---
title: "${concept.name}"
kind: concept
---

# ${concept.name}

${concept.brief ? `## Summary\n\n${concept.brief}\n` : ""}

## Definition
\`\`\`cpp
${concept.defn ? `${concept.defn};` : ""}
\`\`\`

${concept.detailed ? `## Detailed Description\n\n${concept.detailed}\n` : ""}
`;

        await fs.writeFile(path.join(CONCEPTS_OUT_DIR, outFile), md);

        index.push({
            name: concept.name,
            file: outFile,
        });
    }

    /// Finally, create the index.md page.
    index.sort((a, b) => a.name.localeCompare(b.name));

    const indexMd = `---
title: Concepts
---

# Concepts

${index
  .map((c) => `- [${c.name}](./${c.file.replace(".md", "")})`)
  .join("\n")}
`;

    await fs.writeFile(path.join(CONCEPTS_OUT_DIR, "index.md"), indexMd);

    console.log(`✔ Generated ${index.length} concepts`);
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});

// -----------------------------------------------------------------------------
// Finally, create a global API reference index file to link to each subfolder.
// -----------------------------------------------------------------------------

const rootFolders = Object.keys(tree).sort((a, b) =>
    safeStr(a).localeCompare(safeStr(b))
);

let root = `---
title: API Reference
---

# API Reference

`;

for (const f of rootFolders) {
    root += `- [${f}](./${f.toLowerCase()}/)\n`;
}

root += `- [Concepts](./concepts/)\n`; /// The Concepts wasnt in the initial tree

await fs.writeFile(path.join(MD_DIR, "index.md"), root);


console.log("✔ API index generated successfully");
