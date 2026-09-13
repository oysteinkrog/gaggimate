// The web form's half of the rollback mirror and the legacy custom fallback.
//
// Run with `node tools/gradient_mirror_check.mjs` from the repo root. Exits 0
// when every case holds, 1 with the failures listed otherwise. No test runner
// is installed under web/, and these are pure functions, so this file imports
// them straight out of web/src/config/bgAnimations.js.
//
// The firmware's half of the same policy is bg_legacy_mirror_for_ref and
// bg_legacy_builtin in src/display/ui/default/bganim/BgAnim.h, checked by
// `pio test -e native_settingsui`. The two must agree: bgAnimTheme is what a
// build rolled back to a shorter table reads, so a mirror that is wrong on one
// side draws a gradient nobody chose.

import {
  BG_GRADIENT_ID_MAX,
  BG_GRADIENT_LIB_MAX,
  BG_LEGACY_CUSTOM_NAME,
  BG_LEGACY_CUSTOM_REF,
  BG_THEMES,
  BG_THEME_CUSTOM,
  globalAssignFields,
  globalGradientRef,
  gradientForRef,
  gradientLibraryValid,
  gradientRefValid,
  legacyBuiltin,
  legacyThemeMirror,
  nextGradientId,
  parseGradientLibrary,
  parseThemeMap,
  refResolves,
  reservedGradientIds,
  serializeGradient,
  serializeGradientLibrary,
  themeMapValid,
} from '../web/src/config/bgAnimations.js';

const failures = [];
let checks = 0;

function check(name, got, want) {
  checks++;
  const a = JSON.stringify(got);
  const b = JSON.stringify(want);
  if (a !== b) failures.push(`${name}: got ${a}, want ${b}`);
}

// The sentinel is frozen at 18 and not read off the table length. The whole
// bead turns on this one line.
check('sentinel frozen at 18', BG_THEME_CUSTOM, 18);

// ---- the mirror policy ----------------------------------------------------
const BIG = 60;
for (let t = 0; t < 18; t++) {
  check(`built-in ${t} mirrors unchanged`, legacyThemeMirror(String(t), BIG), t);
}
check('built-in 17 mirrors unchanged', legacyThemeMirror('17', BIG), 17);
check('built-in 18 mirrors as 0', legacyThemeMirror('18', BIG), 0);
check('built-in 30 mirrors as 0', legacyThemeMirror('30', BIG), 0);
check('built-in 59 mirrors as 0', legacyThemeMirror('59', BIG), 0);
check('a library ref leaves the mirror alone', legacyThemeMirror('c1', BIG), null);
check('an empty ref leaves the mirror alone', legacyThemeMirror('', BIG), null);
check('a malformed ref leaves the mirror alone', legacyThemeMirror('3x', BIG), null);
check('an index this build lacks leaves the mirror alone', legacyThemeMirror('30', 18), null);
check('the default theme count is this build table', legacyThemeMirror('17'), 17);

// ---- what a selection writes ---------------------------------------------
check('selecting built-in 17 writes both fields', globalAssignFields('17', BIG), {
  bgAnimGradientRef: '17',
  bgAnimTheme: '17',
});
check('selecting built-in 18 mirrors as 0', globalAssignFields('18', BIG), {
  bgAnimGradientRef: '18',
  bgAnimTheme: '0',
});
check('selecting built-in 30 mirrors as 0', globalAssignFields('30', BIG), {
  bgAnimGradientRef: '30',
  bgAnimTheme: '0',
});
check('selecting a library entry leaves the mirror alone', globalAssignFields('c1', BIG), {
  bgAnimGradientRef: 'c1',
});
check('the legacy stand-in writes nothing', globalAssignFields(BG_LEGACY_CUSTOM_REF, BIG), {});

// An appended built-in then a library entry, before anything is saved. The
// mirror must not be left holding an appended index.
{
  const form = { bgAnimTheme: '7', bgAnimGradientRef: '' };
  Object.assign(form, globalAssignFields('30', BIG));
  check('after picking built-in 30 the mirror is 0', form.bgAnimTheme, '0');
  Object.assign(form, globalAssignFields('c1', BIG));
  check('after picking a library entry the mirror is still 0', form.bgAnimTheme, '0');
  check('and the ref is the library entry', form.bgAnimGradientRef, 'c1');
}

// ---- the legacy pair, read ------------------------------------------------
const CUSTOM = '112233,445566,778899';
check('18 with a valid custom string draws the custom string', legacyBuiltin(18, CUSTOM), -1);
check('18 with an empty one falls back to built-in 0', legacyBuiltin(18, ''), 0);
check('18 with a malformed one falls back to built-in 0', legacyBuiltin(18, 'zzz'), 0);
check('a legacy built-in reads as itself', legacyBuiltin(5, CUSTOM), 5);
check('a legacy integer past 18 reads as built-in 0', legacyBuiltin(30, CUSTOM), 0);
check('a negative legacy integer reads as built-in 0', legacyBuiltin(-1, CUSTOM), 0);

// ---- the deferred legacy fallback, shown ----------------------------------
{
  const library = [];
  const ref = globalGradientRef('', library, 18, CUSTOM);
  check('a retained legacy custom reads as the stand-in', ref, BG_LEGACY_CUSTOM_REF);
  const shown = gradientForRef(ref, library, CUSTOM);
  check('it is named for what it is', shown.name, BG_LEGACY_CUSTOM_NAME);
  check('it is never named after built-in 18', shown.name === BG_THEMES[0].name, false);
  check('it is read only', shown.editable, false);
  check('it previews the stored stops', shown.stops.map(s => s.color), [
    '#112233',
    '#445566',
    '#778899',
  ]);
  check('and the stops are evenly spaced, as the firmware draws them', shown.stops.map(s => s.pos), [
    0, 127, 255,
  ]);
  // Nothing above wrote a field. Opening the editor calls exactly these
  // functions, and cancelling calls none of them.
  check('showing it writes nothing', globalAssignFields(ref, BIG), {});
}
// ---- a legacy custom string that carries positions ------------------------
// The firmware's last fallback throws the stored positions away: it parses the
// colours out of bgAnimCustomTheme and then spaces them evenly on the uniform
// path, whatever the string said (BgAnimThemes.cpp, bg_resolve_anim_theme step
// three). The web has to show that gradient, not the positioned one the string
// spells, or the panel draws something the page never displayed.
//
// Every fixture above is written without positions, so the positions the web
// returned were even by accident and the whole path went unchecked (gm-nov3.18).
const POSITIONED = 'ff0000@0,00ff00@10,0000ff@255';
{
  const ref = globalGradientRef('', [], 18, POSITIONED);
  check('a positioned legacy custom still reads as the stand-in', ref, BG_LEGACY_CUSTOM_REF);
  const shown = gradientForRef(ref, [], POSITIONED);
  check('it keeps the stored colours', shown.stops.map(s => s.color), [
    '#ff0000',
    '#00ff00',
    '#0000ff',
  ]);
  check('the stored positions are dropped, as the firmware drops them', shown.stops.map(s => s.pos), [
    0, 127, 255,
  ]);
  // The editor sends exactly this string to the panel as a live preview, so a
  // positioned serialization would make the panel draw the wrong gradient for
  // as long as the editor is open. No @pos at all is what puts the firmware's
  // parser back on the uniform path.
  check(
    'it serializes for the preview with no positions',
    serializeGradient({ stops: shown.stops }),
    'ff0000,00ff00,0000ff',
  );
  check('showing a positioned one writes nothing too', globalAssignFields(ref, BIG), {});
}
{
  // Only the legacy representation is normalized. A library gradient is drawn
  // by the positional path and keeps every position it was saved with.
  const library = [
    { id: 1, name: 'Mine', stops: [
      { color: '#ff0000', pos: 0 },
      { color: '#00ff00', pos: 10 },
      { color: '#0000ff', pos: 255 },
    ] },
  ];
  const shown = gradientForRef('c1', library, POSITIONED);
  check('a library gradient keeps its own positions', shown.stops.map(s => s.pos), [0, 10, 255]);
  check('and serializes with them', serializeGradient({ stops: shown.stops }), 'ff0000@0,00ff00@10,0000ff@255');
}
{
  // A legacy custom string the firmware's parser rejects is not the stand-in at
  // all: bg_resolve_theme sends it to built-in 0, and so does the web.
  check('an empty positioned-era string falls back to a built-in', globalGradientRef('', [], 18, ''), '0');
  check('a malformed one falls back to a built-in', globalGradientRef('', [], 18, 'ff0000@300,00ff00'), '0');
  check('a one-stop one falls back to a built-in', globalGradientRef('', [], 18, 'ff0000@10'), '0');
}
{
  // A resolving ref wins over the legacy pair, and an empty legacy custom
  // string sends the fallback to the built-in, never to the stand-in.
  check('a resolving global ref wins', globalGradientRef('5', [], 18, CUSTOM), '5');
  check('an empty custom string falls back to a built-in', globalGradientRef('', [], 18, ''), '0');
  check('a legacy built-in shows itself', globalGradientRef('', [], 7, ''), '7');
  check('a legacy integer past 18 shows built-in 0', globalGradientRef('', [], 30, CUSTOM), '0');
}

// ---- the id grammar, against the firmware's own answers -------------------
// Every expected value below was read off the firmware parsers themselves,
// compiled on the host out of src/display/ui/default/bganim/BgAnimThemes.cpp
// and asked the same 23 strings (gm-nov3.21). They are not guesses about what
// the C++ does. The reason the page has to agree: WebUIPlugin.cpp's settings
// handler drops a library, a map or a ref its parser rejects and keeps the
// previous stored value, so a page that accepts a string the firmware does not
// loses the save with no error on either side.
//
// The boundary is parseId reading at most five decimal digits. 100000 is not a
// large id to the firmware, it is a malformed one.
check('the id ceiling mirrors BG_GRADIENT_ID_MAX', BG_GRADIENT_ID_MAX, 99999);
for (const [ref, want] of [
  ['', true],
  ['99999', true],
  ['100000', false],
  ['c99999', true],
  ['c100000', false],
  ['c0', false],
  ['5x', false],
  ['c5x', false],
  ['c', false],
  ['c 5', false],
  ['099999', false],
]) {
  check(`ref ${JSON.stringify(ref)} is ${want ? 'valid' : 'rejected'}`, gradientRefValid(ref), want);
}
for (const [map, want] of [
  ['c99999', true],
  ['c100000', false],
  [';;c99999;', true],
  ['c0', false],
  ['5x', false],
]) {
  check(`map ${JSON.stringify(map)} is ${want ? 'valid' : 'rejected'}`, themeMapValid(map), want);
}
for (const [lib, want] of [
  ['99999|A|010203,040506', true],
  ['100000|A|010203,040506', false],
  ['0|A|010203,040506', false],
  ['99999x|A|010203,040506', false],
  ['1|A|010203,040506;bad;2|B|010203,040506', false],
  ['1||010203,040506', false],
  ['', true],
]) {
  check(
    `library ${JSON.stringify(lib)} is ${want ? 'valid' : 'rejected'}`,
    gradientLibraryValid(lib),
    want,
  );
}

// A map slot the firmware cannot read is not a selection the page may show.
check('a map slot of c100000 reads as no override', parseThemeMap('c100000')[0], '');
check('a map slot of c0 reads as no override', parseThemeMap('c0')[0], '');
check('a map slot of c99999 is kept', parseThemeMap('c99999')[0], 'c99999');

// The library walk stops at the first malformed entry, as walkLibrary does,
// rather than stepping over it: the firmware's lookup cannot see past it.
check(
  'the walk stops at the first malformed entry',
  parseGradientLibrary('1|A|010203,040506;bad;2|B|010203,040506').map(g => g.id),
  [1],
);
check(
  'an id of six digits ends the walk',
  parseGradientLibrary('100000|A|010203,040506').map(g => g.id),
  [],
);
check(
  'an id of five digits is read',
  parseGradientLibrary('99999|A|010203,040506').map(g => g.id),
  [99999],
);

// ---- allocating an id -----------------------------------------------------
// The defect this replaces: the allocator returned the largest id plus one, so
// a library holding 99999 handed out 100000 and the ref "c100000". Both are
// strings the firmware rejects, and the save then vanished in silence.
{
  const library = [
    {
      id: 3,
      name: 'A',
      stops: [
        { color: '#010203', pos: 0 },
        { color: '#040506', pos: 255 },
      ],
    },
    {
      id: 99999,
      name: 'Top',
      stops: [
        { color: '#0a0b0c', pos: 0 },
        { color: '#0d0e0f', pos: 255 },
      ],
    },
  ];
  const id = nextGradientId(library, ['', '']);
  check('a library holding 99999 still allocates inside the range', id <= BG_GRADIENT_ID_MAX, true);
  check('and it takes the lowest free id, not one past the top', id, 1);
  check('the ref it makes is one the firmware reads', gradientRefValid(`c${id}`), true);
  const next = [...library, { id, name: 'Copy', stops: library[1].stops }];
  check(
    'and the library it makes is one the firmware reads',
    gradientLibraryValid(serializeGradientLibrary(next)),
    true,
  );
}

// Reuse must not resurrect a dangling ref. Deleting entry 7 can leave "c7" in
// the global ref or in a map slot; handing 7 to the next gradient would make
// that ref resolve again and point a saved selection at a picture nobody chose
// for it. This is the criterion the whole change turns on.
{
  const library = [{ id: 1, name: 'A', stops: [] }];
  const map = ';;c2;;c7';
  const globalRef = 'c3';
  const taken = reservedGradientIds(library, [map, globalRef]);
  check('a dangling map ref is reserved', taken.has(2), true);
  check('a second dangling map ref is reserved', taken.has(7), true);
  check('a dangling global ref is reserved', taken.has(3), true);
  const id = nextGradientId(library, [map, globalRef]);
  check('the allocated id is the lowest one nothing names', id, 4);
  check(
    'it is absent from the library',
    library.some(g => g.id === id),
    false,
  );
  check('it is not the global ref', `c${id}` === globalRef, false);
  check('it is in no map slot', map.split(';').includes(`c${id}`), false);
}

// Raw stored strings go in, not the parsed arrays: parsing drops exactly the
// refs that matter, a ref that no longer resolves and a slot past the end of
// this build's animation list.
{
  const library = [{ id: 1, name: 'A', stops: [] }];
  const map = new Array(200).fill('').concat(['c5']).join(';');
  check('a slot past this list still reserves', reservedGradientIds(library, [map]).has(5), true);
  check('the parsed map cannot see that slot', parseThemeMap(map).includes('c5'), false);
}

// A built-in ref reserves nothing: it names a table index, not a library id.
check('a built-in ref reserves no library id', reservedGradientIds([], ['5;7']).size, 0);
// Neither does a ref the firmware cannot read, because the firmware's own
// ReservedIds cannot read it either and such a string never reaches storage.
check('an unreadable ref reserves nothing', reservedGradientIds([], ['c100000']).size, 0);

// No safe id left is an explicit result, not a silent allocation past the
// ceiling. It is not reachable from a device (at most BG_GRADIENT_LIB_MAX
// entries and one ref per animation reserve a few dozen ids out of 99999), so
// the case is driven here with a synthetic library.
{
  const full = Array.from({ length: BG_GRADIENT_ID_MAX }, (_, i) => ({ id: i + 1 }));
  check('an exhausted range reports unavailable', nextGradientId(full, ['']), null);
  const nearly = full.slice(0, BG_GRADIENT_ID_MAX - 1);
  check(
    'one id short of exhausted still allocates',
    nextGradientId(nearly, ['']),
    BG_GRADIENT_ID_MAX,
  );
  check('and 99999 is a ref the firmware reads', gradientRefValid(`c${BG_GRADIENT_ID_MAX}`), true);
}

// ---- a copy at the boundary, saved and reopened ---------------------------
// The round trip the user makes: a library already holding 99999, copy the top
// gradient, save, reload the form from the stored strings, and find the same
// ref selected over the same stops.
{
  const stops = [
    { color: '#0a0b0c', pos: 0 },
    { color: '#112233', pos: 128 },
    { color: '#0d0e0f', pos: 255 },
  ];
  const before = [{ id: BG_GRADIENT_ID_MAX, name: 'Top', stops }];
  const id = nextGradientId(before, ['', '']);
  const after = [...before, { id, name: 'Top copy', stops }];
  const storedLibrary = serializeGradientLibrary(after);
  const storedRef = `c${id}`;
  check('the stored library is one the firmware accepts', gradientLibraryValid(storedLibrary), true);
  check('the stored ref is one the firmware accepts', gradientRefValid(storedRef), true);
  check('the stored map is one the firmware accepts', themeMapValid(storedRef), true);
  const reopened = parseGradientLibrary(storedLibrary);
  check(
    'both entries come back',
    reopened.map(g => g.id),
    [BG_GRADIENT_ID_MAX, id],
  );
  check('the copy is still selectable', refResolves(storedRef, reopened), true);
  check(
    'and 99999 is still selectable beside it',
    refResolves(`c${BG_GRADIENT_ID_MAX}`, reopened),
    true,
  );
  const shown = gradientForRef(storedRef, reopened, '');
  check('the reopened copy has the stops it was saved with', shown.stops, stops);
  check('a library selection leaves the rollback mirror alone', globalAssignFields(storedRef), {
    bgAnimGradientRef: storedRef,
  });
  check('the library stays inside the entry cap', after.length <= BG_GRADIENT_LIB_MAX, true);
}

if (failures.length > 0) {
  console.error(`gradient_mirror_check: ${failures.length} of ${checks} checks failed`);
  for (const f of failures) console.error(`  ${f}`);
  process.exit(1);
}
console.log(`gradient_mirror_check: ${checks} checks passed`);
