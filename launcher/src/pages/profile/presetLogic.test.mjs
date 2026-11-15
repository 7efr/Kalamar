import test from "node:test";
import assert from "node:assert/strict";
import { applyPreset, loadoutOf, matches, sameName } from "./presetLogic.ts";

const base = { schema: 1, name: "K", hair: 4, head: 1, clothes: 2, shoes: 3, weapon: 10, sub_abilities: { head: ["A", null, null], clothes: null, shoes: null } };
const preset = { name: "P", head: 5, clothes: 6, shoes: null, weapon: 20, sub_abilities: { head: ["B", "C", null], clothes: null, shoes: null } };
const all = { has: () => true, subs: true };

test("a preset copies the five loadout fields and nothing else", () => {
  const { profile, skipped } = applyPreset(base, preset, all);
  assert.equal(skipped, 0);
  assert.deepEqual([profile.head, profile.clothes, profile.shoes, profile.weapon], [5, 6, null, 20]);
  assert.deepEqual(profile.sub_abilities.head, ["B", "C", null]);
  assert.equal(profile.hair, 4);
  assert.equal(profile.name, "K");
});

test("without sub-ability support they are left alone", () => {
  const { profile } = applyPreset(base, preset, { ...all, subs: false });
  assert.deepEqual(profile.sub_abilities, base.sub_abilities);
  assert.equal(profile.head, 5);
});

test("an item the game data no longer has is skipped with its sub-abilities", () => {
  const { profile, skipped } = applyPreset(base, preset, { has: (f, id) => !(f === "head" && id === 5), subs: true });
  assert.equal(skipped, 1);
  assert.equal(profile.head, 1);
  assert.deepEqual(profile.sub_abilities.head, ["A", null, null]);
  assert.equal(profile.weapon, 20);
});

test("an ability the game cannot put in a sub slot is emptied", () => {
  const { profile } = applyPreset(base, preset, { ...all, abilities: ["B"] });
  assert.deepEqual(profile.sub_abilities.head, ["B", null, null]);
});

test("the current loadout is recognised", () => {
  const applied = applyPreset(base, preset, all).profile;
  assert.equal(matches(preset, applied, true), true);
  assert.equal(matches(preset, base, true), false);
  // Never edited and three empty slots are the same thing.
  assert.equal(matches(loadoutOf(base), { ...base, sub_abilities: { head: ["A", null, null], clothes: [null, null, null], shoes: null } }, true), true);
  // Sub-abilities are ignored when unsupported.
  assert.equal(matches({ ...loadoutOf(base), sub_abilities: { head: ["Z", null, null], clothes: null, shoes: null } }, base, false), true);
});

test("names compare trimmed and case-insensitively", () => {
  assert.equal(sameName(" Ranked", "ranked "), true);
  assert.equal(sameName("a", "b"), false);
});
