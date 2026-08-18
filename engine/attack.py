"""How much garbage a placement would send, and the rates built on top of it.

Forcetris has no opponent to send garbage to, but the number is what APM and VS
- the figures TETR.IO players actually train by - are made of, so it is scored
as TETR.IO would score it and put on the analysis screen next to PPS.

The table is TETR.IO's: quads send four, spins send double their lines, minis
send no more than a plain clear, back to back adds one, combos climb the classic
combo table, and a perfect clear adds ten on top of whatever earned it. Under
the all-spin rules every full spin uses the spin line and every mini the mini
line, which is what all-spin means competitively.

This module is ported to C++ in cpp/src/attack.cpp and the two are graded
against each other by the equivalence test, so a change here that is not made
there will fail the build rather than quietly fork the numbers.
"""

# Lines sent by a plain clear of 0..4 lines.
BASE = (0, 0, 1, 2, 4)
# The same clear made as a full spin: T-spin numbers, which all-spin extends to
# every piece. A spin quad only exists under all-spin, and sends ten.
SPIN = (0, 2, 4, 6, 10)
# And as a mini: the same as a plain clear.
MINI = (0, 0, 1, 2, 4)
# The bonus for the combo the clear extended: index is the combo count shown on
# the HUD, so the first clear of a run is index 0. Runs past the end stay at the
# last entry.
COMBO = (0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5)
# Keeping back to back alive adds one.
B2B = 1
# Emptying the board adds ten on top.
PERFECT = 10

# What kind of spin a placement was, as attack_for takes it.
NOT_SPIN, SPIN_MINI, SPIN_FULL = range(3)


def spin_kind (label):
	# The banner already says everything the table needs: '' for no spin,
	# 'T-SPIN' for a full one, 'MINI S-SPIN' for the lesser one.
	if not label:
		return NOT_SPIN
	return SPIN_MINI if label.startswith('MINI') else SPIN_FULL


def attack_for (lines, spin=NOT_SPIN, b2b=False, combo=0, perfect=False):
	"""Garbage sent by one placement.

	`lines` is how many rows the placement took. `spin` is one of the three
	kinds above. `b2b` is whether this clear extended a back to back run - the
	caller decides that, since only it knows the chain. `combo` is the combo
	count as shown on the HUD (first clear of a run is 0). `perfect` is a
	perfect clear.
	"""
	if lines <= 0:
		# A placement that cleared nothing sends nothing, spin or not.
		return 0
	# Cascade clearing can take more than four rows in one placement; the table
	# tops out at a quad rather than inventing numbers TETR.IO never defined.
	step = min(lines, 4)
	table = (BASE, MINI, SPIN)[spin]
	sent = table[step]
	if b2b:
		sent += B2B
	sent += COMBO[min(max(combo, 0), len(COMBO) - 1)]
	if perfect:
		sent += PERFECT
	return sent


def apm (attack, seconds):
	# Attack per minute, the rate TETR.IO shows first.
	return 0. if seconds <= 0 else attack * 60. / seconds


def vs_score (attack, downstack, seconds):
	# VS: attack and garbage lines dug out, per hundred seconds. In a game with
	# no garbage the downstack half is simply zero and VS reads as APS x100.
	return 0. if seconds <= 0 else (attack + downstack) * 100. / seconds
