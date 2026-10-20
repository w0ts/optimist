// SPDX-License-Identifier: GPL-3.0-only
// The simulator panel's button lookup: a drawn label -> the firmware's button index.

const plain = (s) => s.replace(/−/g, "-").replace(/\s/g, "").toUpperCase();   /* (the panel draws OCT− with a true minus) */
const alnum = (s) => s.replace(/[^A-Z0-9]/gi, "");

/* the index in `names` of the button drawn as `label`, or -1. The exact name wins: OCT- and OCT+ are the same
   word once the punctuation is dropped, so the loose matches only run when no name is exactly the label */
export function findButton(names, label) {
  const exact = names.findIndex((n) => plain(n) === plain(label));
  if (exact >= 0) return exact;
  const loose = names.findIndex((n) => alnum(n) === alnum(label));
  return loose >= 0 ? loose : names.findIndex((n) => n.startsWith(label.slice(0, 3)));
}
