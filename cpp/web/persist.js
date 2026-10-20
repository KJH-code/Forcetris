// The saves, in a browser.
//
// Everything the game writes - the map, the career, the profile, the
// scores, the replays and the settings - goes under <root>/data, and on
// the web the root is /game. That folder is the only part of the game's
// world that must outlive the tab, so it is not part of the heap: it is a
// mount over IndexedDB, read back in full before main runs and written
// out again while it runs.
//
// This is a --pre-js, not part of the page, for two reasons. It is inside
// the module, so FS, IDBFS and the run-dependency counter are simply in
// scope; and naming IDBFS here is what keeps it in the build at all -
// emscripten drops the filesystem nobody mentions.

Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(() => {
	FS.mkdirTree('/game/data');
	FS.mount(IDBFS, {}, '/game/data');
	// main must not start before the read-back lands, or the game looks
	// for a save in the half second before there is one and decides the
	// player is new. A run dependency is the runtime's own way of being
	// told to wait.
	addRunDependency('forcetris-saves');
	FS.syncfs(true, (err) => {
		if (err) { console.warn('forcetris: saves not read', err); }
		removeRunDependency('forcetris-saves');
	});
});

// The other direction, for the page to call: one sync at a time, because
// a second one started mid-write would race the first for the same keys.
var forcetrisSyncing = false;
Module['forcetrisFlush'] = (then) => {
	if (forcetrisSyncing) { return; }
	forcetrisSyncing = true;
	FS.syncfs(false, (err) => {
		forcetrisSyncing = false;
		if (err) { console.warn('forcetris: saves not written', err); }
		if (then) { then(); }
	});
};

// A phone is a different game: the layouts, the touch pads and the
// portrait board are all behind one switch the desktop build reads out of
// the environment. A browser has no environment to read, but emscripten
// keeps one for it, and a page can fill it in before main looks.
//
// The test is the pointer, not the width: a narrow window on a desk is
// still a mouse, and a tablet held in two hands is still a thumb. The
// size handed over is the tab's own - SDL takes the canvas at its CSS
// size anyway, so this only says which shape the layouts should assume.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(() => {
	const touch = window.matchMedia
		&& window.matchMedia('(pointer: coarse)').matches
		&& window.matchMedia('(hover: none)').matches;
	if (touch) {
		ENV['FORCETRIS_MOBILE']
			= Math.round(window.innerWidth) + 'x' + Math.round(window.innerHeight);
	}
});
