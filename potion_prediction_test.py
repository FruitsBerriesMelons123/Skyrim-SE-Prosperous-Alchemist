#!/usr/bin/env python3
"""Standalone Prosperous Alchemist potion-value prediction test harness.

This script mirrors the value-bearing code in ``alchemist/include/main.h`` and
``alchemist/CACO/CACO.h``.  It intentionally reads exported ingredient data
instead of requiring Skyrim or a loaded plugin.  Its first purpose is parity
with the current SKSE plugin; the supplied predicted-value CSV files are
regression fixtures for that baseline.  The known CACO 397-versus-228 case is
kept as a later Python-only investigation target and is not treated as a
current-plugin parity requirement.

Example:
	python alchemist/potion_prediction_test.py \
		--caco-enabled false \
		--alchemy-plus-enabled false \
		"Blue Butterfly Wing" "Blue Mountain Flower"

The two enablement switches select the four supported paths:

	CACO  Alchemy Plus  Path
	false false         vanilla Skyrim
	true  false         CACO only
	false true          Alchemy Plus only
	true  true          CACO + Alchemy Plus

The CSV files do not contain live player state, CACO globals, or the optional
Alchemy Plus JSON configuration.  Those values are therefore exposed as
arguments.  The defaults are deterministic and are documented in --help.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import sys
import tempfile
import time
import unittest
from dataclasses import dataclass, field, replace
from datetime import datetime
from pathlib import Path
from typing import Iterable, Mapping, Sequence


SCRIPT_ROOT = Path(__file__).resolve().parent
VANILLA_CSV = SCRIPT_ROOT / "ingredients-vanilla.csv"
CACO_CSV = SCRIPT_ROOT / "ingredients-caco.csv"
PREDICTION_LOG = SCRIPT_ROOT / "potion_prediction_test.log"
TOLERATED_PREDICTION_DIFFERENCE = 3

# These are the same limits used by the C++ helpers.
INT32_MAX = 2_147_483_647
INT32_MIN = -2_147_483_648
FLOAT32_MAX_FINITE = 3.4028234663852886e38
# Keep record-specific metadata in the CSV exports.  Do not hard-code form IDs
# or add other record-specific exceptions to this prediction harness.


def f32(value: float) -> float:
	"""Round a value to the IEEE-754 single precision used by Skyrim records."""
	return struct.unpack("<f", struct.pack("<f", float(value)))[0]


def finite(value: float) -> bool:
	return math.isfinite(value)


def cxx_round_positive(value: float) -> float:
	"""Mirror std::round for the non-negative alchemy values used here."""
	return f32(math.floor(value + 0.5))


def parse_bool(value: str) -> bool:
	normalized = value.strip().lower()
	if normalized in {"1", "true", "yes", "on", "enabled"}:
		return True
	if normalized in {"0", "false", "no", "off", "disabled"}:
		return False
	raise argparse.ArgumentTypeError(
		f"expected true/false (or 1/0), got {value!r}"
	)


def parse_form_id(value: str) -> int:
	text = value.strip()
	if not text:
		return 0
	return int(text, 16) if text.lower().startswith("0x") else int(text, 16)


def bool_field(row: Mapping[str, str], name: str) -> bool:
	value = row[name].strip().lower()
	if value in {"1", "true"}:
		return True
	if value in {"0", "false"}:
		return False
	raise ValueError(f"CSV field {name!r} must be 0 or 1, got {value!r}")


def optional_bool_field(row: Mapping[str, str], name: str) -> bool | None:
	value = row.get(name)
	if value is None or not value.strip():
		return None
	return bool_field(row, name)


def optional_float_field(row: Mapping[str, str], name: str) -> float | None:
	value = row.get(name)
	if value is None or not value.strip():
		return None
	return f32(float(value))


def optional_form_id_field(row: Mapping[str, str], name: str) -> int | None:
	value = row.get(name)
	if value is None or not value.strip():
		return None
	return parse_form_id(value)


def text_list_field(row: Mapping[str, str], name: str) -> tuple[str, ...]:
	value = row.get(name) or ""
	return tuple(item.strip() for item in value.split(";") if item.strip())


def form_id_list_field(row: Mapping[str, str], name: str) -> tuple[int, ...]:
	return tuple(parse_form_id(item) for item in text_list_field(row, name))


@dataclass(frozen=True)
class EffectRecord:
	"""One ingredient effect row from either exported CSV."""

	ingredient_name: str
	form_id: int
	effect_name: str
	effect_form_id: int
	base_cost: float
	magnitude: float
	duration: float
	power_affects_magnitude: bool
	power_affects_duration: bool
	no_magnitude: bool
	no_duration: bool
	beneficial: bool
	harmful: bool
	hostile: bool
	effect_cost: float | None = None
	duration_based_exported: bool = False
	keyword_editor_ids: tuple[str, ...] = ()
	keyword_form_ids: tuple[int, ...] = ()
	source_effect_form_id: int | None = None
	resolved_effect_name: str | None = None
	resolved_effect_form_id: int | None = None
	resolved_effect_editor_id: str | None = None
	resolved_base_cost: float | None = None
	resolved_power_affects_magnitude: bool | None = None
	resolved_power_affects_duration: bool | None = None
	resolved_no_magnitude: bool | None = None
	resolved_no_duration: bool | None = None
	resolved_beneficial: bool | None = None
	resolved_harmful: bool | None = None
	resolved_hostile: bool | None = None
	resolved_duration_based: bool | None = None
	resolved_keyword_editor_ids: tuple[str, ...] = ()
	resolved_keyword_form_ids: tuple[int, ...] = ()
	description: str = ""

	@property
	def source_identity(self) -> int:
		# The C++ evaluator groups by sourceBaseEffect, which is the original
		# effect form ID even when CACO resolves a different active record.
		return self.source_effect_form_id or self.effect_form_id

	@property
	def duration_based(self) -> bool:
		return self.duration_based_exported or self.resolved_duration_based is True

	@property
	def effective_no_duration(self) -> bool:
		return self.no_duration and not self.duration_based


@dataclass(frozen=True)
class IngredientRecord:
	name: str
	form_id: int
	effects: tuple[EffectRecord, ...]


class IngredientDatabase:
	"""CSV-backed ingredient lookup with case-insensitive CLI matching."""

	REQUIRED_COLUMNS = {
		"ingredient_name",
		"form_id",
		"effect_name",
		"effect_form_id",
		"base_cost",
		"magnitude",
		"duration",
		"power_affects_magnitude",
		"power_affects_duration",
		"no_magnitude",
		"no_duration",
		"beneficial",
		"harmful",
		"hostile",
		"keyword_editor_ids",
		"duration_based",
		"source_effect_form_id",
		"resolved_effect_form_id",
	}

	def __init__(
		self,
		ingredients: Mapping[str, Sequence[IngredientRecord]],
		csv_path: Path,
		prefer_highest_form_id: bool = False,
	):
		self._ingredients = {
			name: tuple(records) for name, records in ingredients.items()
		}
		self.csv_path = csv_path
		self._prefer_highest_form_id = prefer_highest_form_id

	@classmethod
	def load(
		cls, csv_path: Path, prefer_highest_form_id: bool = False
	) -> "IngredientDatabase":
		grouped: dict[tuple[str, int], list[EffectRecord]] = {}
		with csv_path.open("r", encoding="utf-8-sig", newline="") as handle:
			reader = csv.DictReader(handle)
			if reader.fieldnames is None:
				raise ValueError(f"{csv_path} has no CSV header")
			missing = cls.REQUIRED_COLUMNS - set(reader.fieldnames)
			if missing:
				raise ValueError(
					f"{csv_path} is missing required columns: {', '.join(sorted(missing))}"
				)
			for line_number, row in enumerate(reader, start=2):
				try:
					name = row["ingredient_name"].strip()
					form_id = parse_form_id(row["form_id"])
					resolved_effect_name = row.get("resolved_effect_name")
					effect = EffectRecord(
						ingredient_name=name,
						form_id=form_id,
						effect_name=row["effect_name"].strip(),
						effect_form_id=parse_form_id(row["effect_form_id"]),
						base_cost=f32(float(row["base_cost"])),
						magnitude=f32(float(row["magnitude"])),
						duration=f32(float(row["duration"])),
						power_affects_magnitude=bool_field(
							row, "power_affects_magnitude"
						),
						power_affects_duration=bool_field(
							row, "power_affects_duration"
						),
						no_magnitude=bool_field(row, "no_magnitude"),
						no_duration=bool_field(row, "no_duration"),
						beneficial=bool_field(row, "beneficial"),
						harmful=bool_field(row, "harmful"),
						hostile=bool_field(row, "hostile"),
						effect_cost=optional_float_field(row, "effect_cost"),
						duration_based_exported=bool_field(row, "duration_based"),
						keyword_editor_ids=text_list_field(row, "keyword_editor_ids"),
						keyword_form_ids=form_id_list_field(row, "keyword_form_ids"),
						source_effect_form_id=optional_form_id_field(
							row, "source_effect_form_id"
						),
						resolved_effect_name=(
							resolved_effect_name.strip()
							if resolved_effect_name and resolved_effect_name.strip()
							else None
						),
						resolved_effect_form_id=optional_form_id_field(
							row, "resolved_effect_form_id"
						),
						resolved_effect_editor_id=(
							row.get("resolved_effect_editor_id", "").strip() or None
						),
						resolved_base_cost=optional_float_field(
							row, "resolved_base_cost"
						),
						resolved_power_affects_magnitude=optional_bool_field(
							row, "resolved_power_affects_magnitude"
						),
						resolved_power_affects_duration=optional_bool_field(
							row, "resolved_power_affects_duration"
						),
						resolved_no_magnitude=optional_bool_field(
							row, "resolved_no_magnitude"
						),
						resolved_no_duration=optional_bool_field(row, "resolved_no_duration"),
						resolved_beneficial=optional_bool_field(row, "resolved_beneficial"),
						resolved_harmful=optional_bool_field(row, "resolved_harmful"),
						resolved_hostile=optional_bool_field(row, "resolved_hostile"),
						resolved_duration_based=optional_bool_field(
							row, "resolved_duration_based"
						),
						resolved_keyword_editor_ids=text_list_field(
							row, "resolved_keyword_editor_ids"
						),
						resolved_keyword_form_ids=form_id_list_field(
							row, "resolved_keyword_form_ids"
						),
						description=row.get("resolved_description", "").strip(),
					)
					if not name or not effect.effect_name:
						raise ValueError("ingredient_name and effect_name are required")
					if effect.effect_form_id == 0:
						raise ValueError("effect_form_id must not be zero")
					grouped.setdefault((name, form_id), []).append(effect)
				except (TypeError, ValueError, KeyError) as error:
					raise ValueError(
						f"invalid row {line_number} in {csv_path}: {error}"
					) from error

		ingredients: dict[str, list[IngredientRecord]] = {}
		for (name, form_id), effects in grouped.items():
			ingredients.setdefault(name, []).append(
				IngredientRecord(name=name, form_id=form_id, effects=tuple(effects))
			)
		for records in ingredients.values():
			records.sort(key=lambda record: record.form_id)
		return cls(ingredients, csv_path, prefer_highest_form_id)

	def names(self) -> list[str]:
		return sorted(self._ingredients)

	def get(self, requested_name: str) -> IngredientRecord:
		requested_form_id: int | None = None
		lookup_name = requested_name
		if "@" in requested_name:
			lookup_name, form_text = requested_name.rsplit("@", 1)
			try:
				requested_form_id = parse_form_id(form_text)
			except ValueError as error:
				raise KeyError(
					f"invalid ingredient form selector in {requested_name!r}"
				) from error

		records = self._ingredients.get(lookup_name)
		if records is None:
			folded = lookup_name.casefold()
			matches = [
				values
				for name, values in self._ingredients.items()
				if name.casefold() == folded
			]
			if len(matches) == 1:
				records = matches[0]
		if records is not None:
			if requested_form_id is not None:
				for record in records:
					if record.form_id == requested_form_id:
						return record
				raise KeyError(
					f"ingredient {lookup_name!r} has no form ID {form_text!r}"
				)
			if len(records) == 1:
				return records[0]
			signatures = {
				tuple(
					(
						effect.effect_form_id,
						effect.base_cost,
						effect.magnitude,
						effect.duration,
					)
					for effect in record.effects
				)
				for record in records
			}
			if len(signatures) == 1:
				return records[0]
			if self._prefer_highest_form_id:
				return records[-1]
			forms = ", ".join(f"0x{record.form_id:X}" for record in records)
			raise KeyError(
				f"ingredient name {lookup_name!r} is ambiguous; use Name@FORM_ID "
				f"(available: {forms})"
			)
		raise KeyError(
			f"ingredient {requested_name!r} was not found in {self.csv_path.name}"
		)


@dataclass
class PlayerSettings:
	"""The Player fields used by the prediction pipeline."""

	alchemy_level: float = 0.0
	fortify_alchemy_level: float = 0.0
	alchemist_perk_rank: int = 0
	alchemist_perk_multiplier: float | None = None
	purity: bool = False
	physician: bool = False
	benefactor: bool = False
	poisoner: bool = False
	seeker_of_shadows: bool = False
	caco_physician_multiplier: float | None = None
	caco_benefactor_multiplier: float | None = None
	caco_poisoner_multiplier: float | None = None
	caco_seeker_multiplier: float | None = None

	def fallback_alchemist_multiplier(self) -> float:
		if (
			self.alchemist_perk_multiplier is not None
			and finite(self.alchemist_perk_multiplier)
			and self.alchemist_perk_multiplier > 0.0
		):
			return f32(self.alchemist_perk_multiplier)
		return f32(1.0 + self.alchemist_perk_rank * 0.2)

	def caco_perk_multiplier(self, field_name: str, fallback: float) -> float:
		value = getattr(self, field_name)
		if value is not None and finite(value) and value > 0.0:
			return f32(value)
		return f32(fallback)


@dataclass(frozen=True)
class RoundingOverride:
	magnitude_threshold: float | None = None
	magnitude_multiple: float | None = None
	duration_threshold: float | None = None
	duration_multiple: float | None = None


@dataclass
class AlchemyPlusSettings:
	"""The supported portions of AlchemyPlus.json."""

	rounding_enabled: bool = True
	impure_cost_fix_enabled: bool = True
	magnitude_threshold: float = 9999.0
	magnitude_multiple: float = 0.01
	duration_threshold: float = 86313600.0
	duration_multiple: float = 1.0
	overrides: dict[int, RoundingOverride] = field(default_factory=dict)

	@classmethod
	def defaults(cls) -> "AlchemyPlusSettings":
		return cls()

	@classmethod
	def from_json(cls, path: Path) -> "AlchemyPlusSettings":
		try:
			root = json.loads(path.read_text(encoding="utf-8"))
		except (OSError, json.JSONDecodeError) as error:
			raise ValueError(f"could not read Alchemy Plus config {path}: {error}") from error
		if not isinstance(root, dict):
			raise ValueError("Alchemy Plus config root must be an object")

		rounded = root.get("roundedPotency")
		rounding_enabled = isinstance(rounded, dict) and rounded.get("enabled") is True
		settings = cls(
			rounding_enabled=rounding_enabled,
			impure_cost_fix_enabled=(
				isinstance(root.get("impureCostFix"), dict)
				and root["impureCostFix"].get("enabled") is True
			),
		)
		if rounding_enabled:
			required = (
				"magnitudeThreshold",
				"magnitudeMult",
				"durationThreshold",
				"durationMult",
			)
			if not all(isinstance(rounded.get(name), (int, float)) for name in required):
				raise ValueError("roundedPotency is missing a numeric global setting")
			settings.magnitude_threshold = f32(float(rounded["magnitudeThreshold"]))
			settings.magnitude_multiple = f32(float(rounded["magnitudeMult"]))
			settings.duration_threshold = f32(float(rounded["durationThreshold"]))
			settings.duration_multiple = f32(float(rounded["durationMult"]))
			if (
				not all(
					finite(value)
					for value in (
						settings.magnitude_threshold,
						settings.magnitude_multiple,
						settings.duration_threshold,
						settings.duration_multiple,
					)
				)
				or settings.magnitude_multiple <= 0.0
				or settings.duration_multiple <= 0.0
			):
				raise ValueError("roundedPotency contains invalid global settings")

			overrides = rounded.get("overrides", {})
			if overrides is not None and not isinstance(overrides, dict):
				raise ValueError("roundedPotency.overrides must be an object")
			for identifier, value in (overrides or {}).items():
				if not isinstance(identifier, str) or not isinstance(value, dict):
					continue
				form_text = identifier.rsplit("|", 1)[-1]
				try:
					form_id = parse_form_id(form_text)
				except ValueError:
					continue
				settings.overrides[form_id] = RoundingOverride(
					magnitude_threshold=read_optional_number(value, "magnitudeThreshold"),
					magnitude_multiple=read_optional_number(value, "magnitudeMult"),
					duration_threshold=read_optional_number(value, "durationThreshold"),
					duration_multiple=read_optional_number(value, "durationMult"),
				)
		return settings

	def _rule(self, effect: EffectRecord, duration: bool) -> tuple[float, float]:
		override = self.overrides.get(effect.effect_form_id)
		if duration:
			threshold = self.duration_threshold
			multiple = self.duration_multiple
			if override is not None:
				if override.duration_threshold is not None:
					threshold = override.duration_threshold
				if override.duration_multiple is not None:
					multiple = override.duration_multiple
		else:
			threshold = self.magnitude_threshold
			multiple = self.magnitude_multiple
			if override is not None:
				if override.magnitude_threshold is not None:
					threshold = override.magnitude_threshold
				if override.magnitude_multiple is not None:
					multiple = override.magnitude_multiple
		return f32(threshold), f32(multiple)

	def apply_magnitude_rounding(self, effect: EffectRecord, value: float) -> float:
		return apply_alchemy_plus_rounding(value, *self._rule(effect, False))

	def apply_duration_rounding(self, effect: EffectRecord, value: float) -> float:
		rounded = apply_alchemy_plus_rounding(value, *self._rule(effect, True))
		if rounded != value and finite(rounded):
			# ApplyDurationRounding casts the rounded value to int32_t before
			# converting it back to float.
			if INT32_MIN <= rounded <= 2_147_483_520.0:
				rounded = f32(int(rounded))
		return rounded


def read_optional_number(values: Mapping[str, object], name: str) -> float | None:
	value = values.get(name)
	if not isinstance(value, (int, float)) or not finite(float(value)):
		return None
	if name.endswith("Mult") and float(value) <= 0.0:
		return None
	return f32(float(value))


def apply_alchemy_plus_rounding(value: float, threshold: float, multiple: float) -> float:
	"""Mirror AlchemyPlus Adapter::ApplyMagnitude/DurationRounding."""
	if (
		not finite(value)
		or not finite(threshold)
		or not finite(multiple)
		or multiple <= 0.0
		or value <= threshold
	):
		return value
	result = f32(value + f32(multiple * 0.5))
	if not finite(result):
		return value
	result = f32(result - f32(math.remainder(result, multiple)))
	return result if finite(result) else value


def floor_gold_value(cost: float) -> int:
	if not finite(cost) or cost <= 0.0:
		return 0
	return min(math.floor(cost), INT32_MAX)


def effect_cost_precise(
	effect: EffectRecord, magnitude: float, duration: float
) -> float:
	"""Mirror CACO::algorithm::CalculateEffectCostPrecise."""
	if not finite(effect.base_cost) or effect.base_cost <= 0.0:
		return 0.0
	cost_magnitude = (
		max(1.0, float(magnitude)) if not effect.no_magnitude else 1.0
	)
	cost_duration = (
		float(duration) / 10.0
		if not effect.effective_no_duration and finite(duration) and duration > 0.0
		else 1.0
	)
	cost = float(effect.base_cost) * math.pow(cost_magnitude, 1.1) * math.pow(
		cost_duration, 1.1
	)
	return cost if finite(cost) and cost > 0.0 else 0.0


def calculate_duration_based_ingredient_power_factor(effectiveness: float) -> float:
	"""Mirror CACO::algorithm::CalculateDurationBasedIngredientPowerFactor."""
	return f32(effectiveness * f32(0.01))


def is_physician_effect(effect: EffectRecord) -> bool:
	normalized = effect.effect_name.casefold()
	return any(
		keyword.casefold().startswith("magicalchrestore")
		for keyword in effect.keyword_editor_ids
	) or normalized in {
		"restore health",
		"restore magicka",
		"restore stamina",
	}


def effect_power_factors(
	effect: EffectRecord,
	player: PlayerSettings,
	potion: bool,
	include_type_perks: bool,
	caco_enabled: bool,
	caco_ingredient_init_multiplier: float,
	caco_skill_factor: float,
	mixed_potion: bool = False,
) -> tuple[float, float]:
	"""Return the magnitude and duration effectiveness factors."""
	fallback = player.fallback_alchemist_multiplier()

	if caco_enabled:
		base = f32(
			f32(caco_ingredient_init_multiplier)
			* f32(
				1.0
				+ f32(
					f32(caco_skill_factor - 1.0)
					* f32(player.alchemy_level)
					/ f32(100.0)
				)
			)
		)
	else:
		base = f32(
			f32(4.0)
			* f32(
				1.0
				+ f32(
					f32(1.5 - 1.0)
					* f32(player.alchemy_level)
					/ f32(100.0)
				)
			)
		)

	magnitude = fallback
	duration = fallback
	if player.physician and not caco_enabled and is_physician_effect(effect):
		perk_multiplier = 1.25
		magnitude = f32(magnitude * perk_multiplier)
		duration = f32(duration * perk_multiplier)
	if include_type_perks:
		if potion and not mixed_potion and player.benefactor and effect.beneficial:
			perk_multiplier = (
				player.caco_perk_multiplier("caco_benefactor_multiplier", 1.25)
				if caco_enabled
				else 1.25
			)
			magnitude = f32(magnitude * perk_multiplier)
			duration = f32(duration * perk_multiplier)
		elif not potion and player.poisoner and not effect.beneficial:
			perk_multiplier = (
				player.caco_perk_multiplier("caco_poisoner_multiplier", 1.25)
				if caco_enabled
				else 1.25
			)
			magnitude = f32(magnitude * perk_multiplier)
			duration = f32(duration * perk_multiplier)
	if player.seeker_of_shadows:
		perk_multiplier = (
			player.caco_perk_multiplier("caco_seeker_multiplier", 1.1)
			if caco_enabled
			else 1.1
		)
		magnitude = f32(magnitude * perk_multiplier)
		duration = f32(duration * perk_multiplier)

	duration_factor = f32(base * duration)
	if caco_enabled and effect.duration_based:
		duration_factor = calculate_duration_based_ingredient_power_factor(duration_factor)
	return f32(base * magnitude), duration_factor


def calculate_effect_input(
	effect: EffectRecord,
	magnitude_factor: float,
	duration_factor: float,
) -> tuple[float, float]:
	"""Mirror CACO::algorithm::CalculateEffectInput."""
	if (
		effect.power_affects_magnitude
		and not effect.no_magnitude
		and (not finite(magnitude_factor) or magnitude_factor <= 0.0)
	) or (
		effect.power_affects_duration
		and not effect.effective_no_duration
		and (not finite(duration_factor) or duration_factor <= 0.0)
	):
		raise ValueError(f"invalid effectiveness factor for {effect.effect_name}")

	magnitude = 0.0 if effect.no_magnitude else f32(max(0.0, effect.magnitude))
	if effect.power_affects_magnitude and not effect.no_magnitude:
		magnitude = cxx_round_positive(f32(magnitude * magnitude_factor))

	duration = 0.0 if effect.effective_no_duration else f32(max(0.0, effect.duration))
	if effect.power_affects_duration and not effect.effective_no_duration:
		duration = cxx_round_positive(f32(duration * duration_factor))
	if (
		(not effect.no_magnitude and not finite(magnitude))
		or (not effect.effective_no_duration and not finite(duration))
	):
		raise ValueError(f"non-finite constructed input for {effect.effect_name}")
	return magnitude, duration


def calculate_legacy_component(
	effect: EffectRecord,
	value: float,
	affects_value: bool,
	magnitude: bool,
	potion: bool,
	include_type_perks: bool,
	player: PlayerSettings,
	caco_enabled: bool,
	caco_ingredient_init_multiplier: float,
	caco_skill_factor: float,
	mixed_potion: bool = False,
) -> float:
	if not affects_value or value <= 0.0:
		return value
	magnitude_factor, duration_factor = effect_power_factors(
		effect,
		player,
		potion,
		include_type_perks,
		caco_enabled,
		caco_ingredient_init_multiplier,
		caco_skill_factor,
		mixed_potion,
	)
	player_factor = f32(1.0 + f32(player.fortify_alchemy_level) * f32(0.01))
	power_factor = magnitude_factor if magnitude else duration_factor
	return cxx_round_positive(
		f32(f32(f32(value) * power_factor) * player_factor)
	)


@dataclass
class CalculatedEffect:
	source: EffectRecord
	calc_magnitude: float
	calc_duration: float
	calc_cost: float
	native_cost: float
	native_order_cost: float


@dataclass
class PredictionResult:
	valid: bool
	mode: str
	ingredients: tuple[str, ...]
	effects: list[CalculatedEffect] = field(default_factory=list)
	is_poison: bool = False
	has_beneficial: bool = False
	has_harmful: bool = False
	pre_adjustment_gold: int = 0
	cost: float = 0.0
	caco_impure_applied: bool = False
	alchemy_plus_impure_applied: bool = False

	@property
	def displayed_value(self) -> int:
		return math.floor(self.cost) if finite(self.cost) else 0


@dataclass
class PredictionSettings:
	caco_enabled: bool
	alchemy_plus_enabled: bool
	player: PlayerSettings = field(default_factory=PlayerSettings)
	caco_ingredient_init_multiplier: float = 3.9
	caco_skill_factor: float = 1.0
	caco_impure_processing: bool = False
	alchemy_plus: AlchemyPlusSettings = field(default_factory=AlchemyPlusSettings)
	prefer_shortest_duration_shared_effect: bool = False

	@property
	def alchemy_plus_rounding(self) -> bool:
		return self.alchemy_plus_enabled and self.alchemy_plus.rounding_enabled

	@property
	def alchemy_plus_impure_cost_fix(self) -> bool:
		return (
			self.alchemy_plus_enabled
			and self.alchemy_plus.impure_cost_fix_enabled
		)

	@property
	def mode(self) -> str:
		if self.caco_enabled and self.alchemy_plus_enabled:
			return "caco+alchemy-plus"
		if self.caco_enabled:
			return "caco"
		if self.alchemy_plus_enabled:
			return "alchemy-plus"
		return "vanilla"


def adjust_impure_effect_cost(
	effect_cost: float,
	is_poison: bool,
	is_hostile: bool,
	enabled: bool,
) -> tuple[float, bool]:
	"""Mirror AlchemyPlus Adapter::AdjustImpureEffectCost."""
	if not enabled or is_poison == is_hostile:
		return effect_cost, False
	return f32(-f32(effect_cost)), True


def finalize_impure_cost(cost: float) -> int:
	"""Mirror AlchemyPlus Adapter::FinalizeImpureCost."""
	if not finite(cost) or cost <= 0.0:
		return 0
	return min(math.floor(cost), INT32_MAX)


class PotionPredictor:
	"""Reproduce the plugin's shared-effect and value aggregation pipeline."""

	def __init__(self, database: IngredientDatabase, settings: PredictionSettings):
		self.database = database
		self.settings = settings

	def evaluate(self, ingredient_names: Sequence[str]) -> PredictionResult:
		if len(ingredient_names) not in {2, 3}:
			raise ValueError("a recipe must contain exactly two or three ingredients")
		ingredients = tuple(self.database.get(name) for name in ingredient_names)
		mode = self.settings.mode
		use_caco_native = self.settings.caco_enabled
		rounding = self.settings.alchemy_plus_rounding

		# effectsBySourceIdentity: each ingredient contributes at most one
		# candidate for a source effect identity.
		candidate_groups: dict[int, list[EffectRecord]] = {}
		for ingredient in ingredients:
			seen_in_ingredient: set[int] = set()
			for effect in ingredient.effects:
				if effect.source_identity in seen_in_ingredient:
					continue
				seen_in_ingredient.add(effect.source_identity)
				candidate_groups.setdefault(effect.source_identity, []).append(effect)

		selected: list[tuple[EffectRecord, float]] = []
		for identity, candidates in candidate_groups.items():
			if len({candidate.form_id for candidate in candidates}) < 2:
				continue
			chosen: EffectRecord | None = None
			chosen_priority = -1.0
			for candidate in candidates:
				if use_caco_native:
					candidate_priority = self._native_effect_order_cost(
						candidate, rounding
					)
				else:
					candidate_priority = self._legacy_effect(
						candidate,
						potion=False,
						include_type_perks=False,
						apply_rounding=rounding,
					).calc_cost
				if not finite(candidate_priority) or candidate_priority < 0.0:
					continue
				prefer_shorter_duration = (
					self.settings.prefer_shortest_duration_shared_effect
					and chosen is not None
					and candidate.duration_based
					and chosen.duration_based
				)
				if chosen is None or (
					candidate_priority < chosen_priority
					if prefer_shorter_duration
					else candidate_priority > chosen_priority
				):
					chosen = candidate
					chosen_priority = candidate_priority
			if chosen is not None:
				selected.append((chosen, chosen_priority))

		if not selected:
			return PredictionResult(False, mode, tuple(ingredient_names))

		selected.sort(
			key=lambda item: (
				-item[1],
				item[0].source_identity,
				item[0].effect_name,
			)
		)
		initial_potion = not selected[0][0].harmful
		if self.settings.player.purity:
			selected = [
				item
				for item in selected
				if not (
					(initial_potion and item[0].harmful)
					or (not initial_potion and item[0].beneficial)
				)
			]
		if not selected:
			return PredictionResult(False, mode, tuple(ingredient_names))

		potion = not selected[0][0].harmful
		mixed_potion = any(effect.harmful for effect, _ in selected)
		calculated: list[CalculatedEffect] = []
		for source, native_order_cost in selected:
			if use_caco_native:
				result = self._native_effect(
					source,
					potion,
					include_type_perks=True,
					apply_rounding=rounding,
					mixed_potion=mixed_potion,
				)
			else:
				result = self._legacy_effect(
					source,
					potion,
					include_type_perks=True,
					apply_rounding=rounding,
					mixed_potion=mixed_potion,
				)
			calculated.append(
				CalculatedEffect(
					source=result.source,
					calc_magnitude=result.calc_magnitude,
					calc_duration=result.calc_duration,
					calc_cost=result.calc_cost,
					native_cost=result.native_cost,
					native_order_cost=native_order_cost,
				)
			)

		has_beneficial = any(effect.source.beneficial for effect in calculated)
		has_harmful = any(effect.source.harmful for effect in calculated)
		is_poison = calculated[0].source.harmful
		caco_impure = (
			use_caco_native
			and has_beneficial
			and has_harmful
			and self.settings.caco_impure_processing
		)

		total_cost = 0.0
		ap_impure = False
		for effect in calculated:
			raw_cost = effect.native_cost if use_caco_native else effect.calc_cost
			if self.settings.alchemy_plus_impure_cost_fix:
				adjusted, marked_impure = adjust_impure_effect_cost(
					f32(raw_cost),
					is_poison,
					effect.source.hostile,
					True,
				)
				total_cost += float(adjusted)
				ap_impure = ap_impure or marked_impure
			elif finite(raw_cost) and raw_cost > 0.0:
				total_cost += float(raw_cost)

		if ap_impure:
			total_cost = float(finalize_impure_cost(total_cost))

		pre_adjustment_gold = floor_gold_value(total_cost)
		cost = (
			f32(pre_adjustment_gold)
			if use_caco_native
			else f32(total_cost) if finite(total_cost) else 0.0
		)

		if caco_impure:
			for effect in calculated:
				if effect.source.duration_based:
					effect.calc_duration = f32(
						math.trunc(float(effect.calc_duration) * 0.2)
					)
				else:
					effect.calc_magnitude = f32(effect.calc_magnitude * 0.2)
			cost = f32(pre_adjustment_gold // 5)

		return PredictionResult(
			valid=True,
			mode=mode,
			ingredients=tuple(ingredient_names),
			effects=calculated,
			is_poison=is_poison,
			has_beneficial=has_beneficial,
			has_harmful=has_harmful,
			pre_adjustment_gold=pre_adjustment_gold,
			cost=cost,
			caco_impure_applied=caco_impure,
			alchemy_plus_impure_applied=ap_impure,
		)

	def _power_factors(
		self,
		effect: EffectRecord,
		potion: bool,
		include_type_perks: bool,
		mixed_potion: bool = False,
	) -> tuple[float, float]:
		return effect_power_factors(
			effect,
			self.settings.player,
			potion,
			include_type_perks,
			self.settings.caco_enabled,
			self.settings.caco_ingredient_init_multiplier,
			self.settings.caco_skill_factor,
			mixed_potion,
		)

	def _legacy_effect(
		self,
		effect: EffectRecord,
		potion: bool,
		include_type_perks: bool,
		apply_rounding: bool,
		mixed_potion: bool = False,
	) -> CalculatedEffect:
		calc_magnitude = calculate_legacy_component(
			effect,
			effect.magnitude,
			effect.power_affects_magnitude,
			True,
			potion,
			include_type_perks,
			self.settings.player,
			False,
			self.settings.caco_ingredient_init_multiplier,
			self.settings.caco_skill_factor,
			mixed_potion,
		)
		calc_duration = calculate_legacy_component(
			effect,
			effect.duration,
			effect.power_affects_duration,
			False,
			potion,
			include_type_perks,
			self.settings.player,
			False,
			self.settings.caco_ingredient_init_multiplier,
			self.settings.caco_skill_factor,
			mixed_potion,
		)
		if apply_rounding:
			if effect.power_affects_magnitude:
				calc_magnitude = self.settings.alchemy_plus.apply_magnitude_rounding(
					effect, calc_magnitude
				)
			if effect.power_affects_duration:
				calc_duration = self.settings.alchemy_plus.apply_duration_rounding(
					effect, calc_duration
				)
		cost = f32(effect_cost_precise(effect, calc_magnitude, calc_duration))
		return CalculatedEffect(
			source=effect,
			calc_magnitude=calc_magnitude,
			calc_duration=calc_duration,
			calc_cost=cost,
			native_cost=cost,
			native_order_cost=cost,
		)

	def _native_input(
		self,
		effect: EffectRecord,
		potion: bool,
		include_type_perks: bool,
		apply_rounding: bool,
		mixed_potion: bool = False,
	) -> tuple[float, float]:
		magnitude_factor = 1.0
		duration_factor = 1.0
		if effect.power_affects_magnitude or effect.power_affects_duration:
			magnitude_factor, duration_factor = self._power_factors(
				effect, potion, include_type_perks, mixed_potion
			)
			player_factor = f32(
				1.0 + f32(self.settings.player.fortify_alchemy_level) * f32(0.01)
			)
			magnitude_factor = f32(magnitude_factor * player_factor)
			duration_factor = f32(duration_factor * player_factor)
		magnitude, duration = calculate_effect_input(
			effect, magnitude_factor, duration_factor
		)
		if apply_rounding:
			if effect.power_affects_magnitude:
				magnitude = self.settings.alchemy_plus.apply_magnitude_rounding(
					effect, magnitude
				)
			if effect.power_affects_duration:
				duration = self.settings.alchemy_plus.apply_duration_rounding(
					effect, duration
				)
		return magnitude, duration

	def _native_effect_order_cost(
		self, effect: EffectRecord, apply_rounding: bool
	) -> float:
		magnitude, duration = self._native_input(
			effect,
			potion=False,
			include_type_perks=False,
			apply_rounding=apply_rounding,
		)
		contribution = effect_cost_precise(effect, magnitude, duration)
		if not finite(contribution) or contribution <= 0.0:
			return 0.0
		return contribution

	def _native_effect(
		self,
		effect: EffectRecord,
		potion: bool,
		include_type_perks: bool,
		apply_rounding: bool,
		mixed_potion: bool = False,
	) -> CalculatedEffect:
		# The plugin calculates nativeContribution before Alchemy Plus rounding,
		# then uses the rounded constructed input for the result contribution.
		native_magnitude, native_duration = self._native_input(
			effect,
			potion,
			include_type_perks,
			apply_rounding=False,
			mixed_potion=mixed_potion,
		)
		native_contribution = effect_cost_precise(
			effect, native_magnitude, native_duration
		)
		if not finite(native_contribution) or native_contribution <= 0.0:
			raise ValueError(f"invalid native contribution for {effect.effect_name}")
		constructed_magnitude, constructed_duration = self._native_input(
			effect,
			potion,
			include_type_perks,
			apply_rounding=apply_rounding,
			mixed_potion=mixed_potion,
		)
		constructed_contribution = effect_cost_precise(
			effect, constructed_magnitude, constructed_duration
		)
		if not finite(constructed_contribution) or constructed_contribution <= 0.0:
			raise ValueError(
				f"invalid constructed contribution for {effect.effect_name}"
			)
		return CalculatedEffect(
			source=effect,
			calc_magnitude=constructed_magnitude,
			calc_duration=constructed_duration,
			calc_cost=float(floor_gold_value(constructed_contribution)),
			native_cost=constructed_contribution,
			native_order_cost=constructed_contribution,
		)


def build_settings(arguments: argparse.Namespace) -> PredictionSettings:
	if arguments.alchemy_plus_config:
		alchemy_plus = AlchemyPlusSettings.from_json(arguments.alchemy_plus_config)
	else:
		alchemy_plus = AlchemyPlusSettings.defaults()

	for field_name, argument_name in (
		("magnitude_threshold", "ap_magnitude_threshold"),
		("magnitude_multiple", "ap_magnitude_multiple"),
		("duration_threshold", "ap_duration_threshold"),
		("duration_multiple", "ap_duration_multiple"),
	):
		value = getattr(arguments, argument_name)
		if value is not None:
			setattr(alchemy_plus, field_name, f32(value))

	if arguments.alchemy_plus_rounding is not None:
		alchemy_plus.rounding_enabled = arguments.alchemy_plus_rounding
	if arguments.alchemy_plus_impure_cost_fix is not None:
		alchemy_plus.impure_cost_fix_enabled = (
			arguments.alchemy_plus_impure_cost_fix
		)

	player = PlayerSettings(
		alchemy_level=f32(arguments.alchemy_level),
		fortify_alchemy_level=f32(arguments.fortify_alchemy),
		alchemist_perk_rank=arguments.alchemist_perk_rank,
		alchemist_perk_multiplier=(
			f32(arguments.alchemist_multiplier)
			if arguments.alchemist_multiplier is not None
			else None
		),
		purity=arguments.purity,
		physician=arguments.physician,
		benefactor=arguments.benefactor,
		poisoner=arguments.poisoner,
		seeker_of_shadows=arguments.seeker_of_shadows,
		caco_physician_multiplier=(
			f32(arguments.caco_physician_multiplier)
			if arguments.caco_physician_multiplier is not None
			else None
		),
		caco_benefactor_multiplier=(
			f32(arguments.caco_benefactor_multiplier)
			if arguments.caco_benefactor_multiplier is not None
			else None
		),
		caco_poisoner_multiplier=(
			f32(arguments.caco_poisoner_multiplier)
			if arguments.caco_poisoner_multiplier is not None
			else None
		),
		caco_seeker_multiplier=(
			f32(arguments.caco_seeker_multiplier)
			if arguments.caco_seeker_multiplier is not None
			else None
		),
	)
	return PredictionSettings(
		caco_enabled=arguments.caco_enabled,
		alchemy_plus_enabled=arguments.alchemy_plus_enabled,
		player=player,
		caco_ingredient_init_multiplier=f32(arguments.caco_ingredient_init),
		caco_skill_factor=f32(arguments.caco_skill_factor),
		caco_impure_processing=(
			arguments.caco_impure_processing
			if arguments.caco_impure_processing is not None
			else False
		),
		alchemy_plus=alchemy_plus,
	)


def format_result(result: PredictionResult, database: IngredientDatabase) -> str:
	lines = [
		f"CSV: {database.csv_path}",
		f"Mode: {result.mode}",
		f"Ingredients: {', '.join(result.ingredients)}",
		f"Type: {'Poison' if result.is_poison else 'Potion'}",
		f"Effects: {len(result.effects)}",
	]
	for effect in result.effects:
		lines.append(
			"  "
			f"{effect.source.effect_name} "
			f"[0x{effect.source.effect_form_id:X}] "
			f"magnitude={effect.calc_magnitude:g} "
			f"duration={effect.calc_duration:g} "
			f"cost={effect.native_cost:.9g} "
			f"order={effect.native_order_cost:.9g}"
		)
	lines.extend(
		[
			f"Beneficial: {result.has_beneficial}",
			f"Harmful: {result.has_harmful}",
			f"Pre-adjustment gold: {result.pre_adjustment_gold}",
			f"Predicted value: {result.cost:.9g}",
			f"Displayed value: {result.displayed_value}",
			f"Alchemy Plus impure adjustment: {result.alchemy_plus_impure_applied}",
			f"CACO impure adjustment: {result.caco_impure_applied}",
		]
	)
	return "\n".join(lines)


def result_json(result: PredictionResult, database: IngredientDatabase) -> str:
	payload = {
		"csv": str(database.csv_path),
		"mode": result.mode,
		"ingredients": list(result.ingredients),
		"valid": result.valid,
		"type": "poison" if result.is_poison else "potion",
		"has_beneficial": result.has_beneficial,
		"has_harmful": result.has_harmful,
		"pre_adjustment_gold": result.pre_adjustment_gold,
		"predicted_value": result.cost,
		"displayed_value": result.displayed_value,
		"alchemy_plus_impure_adjustment": result.alchemy_plus_impure_applied,
		"caco_impure_adjustment": result.caco_impure_applied,
		"effects": [
			{
				"name": effect.source.effect_name,
				"form_id": f"0x{effect.source.effect_form_id:X}",
				"magnitude": effect.calc_magnitude,
				"duration": effect.calc_duration,
				"cost": effect.native_cost,
				"order_cost": effect.native_order_cost,
			}
			for effect in result.effects
		],
	}
	return json.dumps(payload, indent=2, sort_keys=True)


PREDICTION_CSVS = {
	"vanilla": SCRIPT_ROOT / "potions-predicted-vanilla.csv",
	"caco": SCRIPT_ROOT / "potions-predicted-caco.csv",
	"caco+alchemy-plus": SCRIPT_ROOT / "potions-predicted-caco-ap.csv",
	"alchemy-plus": SCRIPT_ROOT / "potions-predicted-ap.csv",
}
PREDICTION_FIXTURE_ORDER = (
	"vanilla",
	"caco",
	"caco+alchemy-plus",
	"alchemy-plus",
)

PARITY_RECIPES = {
	"triple": ("Creep Cluster", "Skeever Tail", "Worm's Head Cap"),
	"pair": ("Creep Cluster", "Skeever Tail"),
	"health": ("Blue Mountain Flower", "Wheat"),
	"impure": ("Hackle-Lo Leaf", "Pygmy Sunfish", "Soul Husk"),
}

PARITY_PERKS = {
	"none": {},
	"seeker": {"seeker_of_shadows": True},
	"physician": {"physician": True},
	"benefactor": {"benefactor": True},
	"poisoner": {"poisoner": True},
	"purity": {"purity": True},
}

def validate_prediction_header(
	reader: csv.DictReader[str], path: Path
) -> None:
	fieldnames = reader.fieldnames or []
	if fieldnames[:2] != ["ingredients", "predicted_value"]:
		raise ValueError(f"{path} has an unexpected prediction CSV header")


def load_prediction_fixture(
	path: Path, recipes: Iterable[tuple[str, ...]]
) -> dict[tuple[str, ...], int]:
	wanted = set(recipes)
	values: dict[tuple[str, ...], int] = {}
	with path.open("r", encoding="utf-8-sig", newline="") as handle:
		reader = csv.DictReader(handle)
		validate_prediction_header(reader, path)
		for row in reader:
			recipe = tuple(item.strip() for item in row["ingredients"].split(","))
			if recipe in wanted:
				values[recipe] = int(row["predicted_value"])
	return values


def prediction_recipe_from_row(
	row: Mapping[str, str], path: Path, line_number: int
) -> tuple[str, ...]:
	display_recipe = tuple(item.strip() for item in row["ingredients"].split(","))
	if len(display_recipe) not in {2, 3} or any(
		not ingredient for ingredient in display_recipe
	):
		raise ValueError(f"{path} row {line_number} has an invalid recipe")

	details = (row.get("ingredient_details") or "").strip()
	if not details or details == "unavailable":
		return display_recipe

	recipe: list[str] = []
	for detail in (item.strip() for item in details.split(";") if item.strip()):
		name, marker, remainder = detail.partition(" [form=")
		form_text, separator, _ = remainder.partition(",")
		if not marker or not separator or not name.strip() or not form_text.strip():
			raise ValueError(
				f"{path} row {line_number} has invalid ingredient details"
			)
		try:
			form_id = parse_form_id(form_text.strip())
		except ValueError as error:
			raise ValueError(
				f"{path} row {line_number} has an invalid ingredient form ID"
			) from error
		recipe.append(f"{name.strip()}@0x{form_id:X}")

	if len(recipe) != len(display_recipe):
		raise ValueError(
			f"{path} row {line_number} ingredient details do not match the recipe"
		)
	return tuple(recipe)


def iter_prediction_fixture(
	path: Path,
) -> Iterable[tuple[int, tuple[str, ...], int]]:
	with path.open("r", encoding="utf-8-sig", newline="") as handle:
		reader = csv.DictReader(handle)
		validate_prediction_header(reader, path)
		for line_number, row in enumerate(reader, start=2):
			recipe = prediction_recipe_from_row(row, path, line_number)
			try:
				expected = int(row["predicted_value"])
			except (TypeError, ValueError) as error:
				raise ValueError(
					f"{path} row {line_number} has an invalid predicted value"
				) from error
			yield line_number, recipe, expected


def log_prediction_mismatch(
	mode: str,
	line_number: int,
	recipe: tuple[str, ...],
	skse_predicted: object,
	python_predicted: object,
	details: str = "",
	path: Path = PREDICTION_LOG,
	prediction_csv: Path | None = None,
) -> None:
	plugin_set = {
		"vanilla": "Vanilla Skyrim (CACO disabled, Alchemy Plus disabled)",
		"caco": "CACO enabled (Alchemy Plus disabled)",
		"caco+alchemy-plus": "CACO and Alchemy Plus enabled",
		"alchemy-plus": "Alchemy Plus enabled (CACO disabled)",
	}.get(mode, mode)
	lines = [
		f"Report time: {datetime.now().astimezone().isoformat(timespec='seconds')}",
		f"Enabled plugin set: {plugin_set}",
		f"Fixture mode: {mode}",
		f"Predicted CSV: {prediction_csv}" if prediction_csv else "Predicted CSV: unavailable",
		f"Fixture row: {line_number}",
		f"Recipe: {', '.join(recipe)}",
		f"Expected value from predicted CSV: {skse_predicted}",
		f"Python predicted: {python_predicted}",
	]
	if details:
		lines.append(f"Details: {details}")
	entry = "\n".join(lines) + "\n\n"
	try:
		previous = path.read_text(encoding="utf-8") if path.exists() else ""
		path.write_text(entry + previous, encoding="utf-8")
	except OSError as error:
		print(f"Could not write prediction mismatch log {path}: {error}", file=sys.stderr)


class InPlaceProgress:
	def __init__(self, stream: object = sys.stdout, update_interval: float = 0.1):
		self._stream = stream
		self._update_interval = update_interval
		self._last_update = 0.0
		self._width = 0

	def update(self, message: str, force: bool = False) -> None:
		now = time.monotonic()
		if not force and now - self._last_update < self._update_interval:
			return
		padding = max(0, self._width - len(message))
		self._stream.write(f"\r{message}{' ' * padding}")
		self._stream.flush()
		self._last_update = now
		self._width = len(message)

	def clear(self) -> None:
		if self._width == 0:
			return
		self._stream.write(f"\r{' ' * self._width}\r")
		self._stream.flush()
		self._width = 0


def run_prediction_fixture_check(
	vanilla_csv: Path = VANILLA_CSV,
	caco_csv: Path = CACO_CSV,
	write_log: bool = True,
) -> int:
	databases: dict[bool, IngredientDatabase] = {}
	progress = InPlaceProgress()
	try:
		for mode in PREDICTION_FIXTURE_ORDER:
			caco_enabled = mode.startswith("caco")
			if caco_enabled not in databases:
				databases[caco_enabled] = IngredientDatabase.load(
					caco_csv if caco_enabled else vanilla_csv,
					prefer_highest_form_id=caco_enabled,
				)
			path = PREDICTION_CSVS[mode]
			checked = 0
			progress.update(f"Checking {mode}: starting", force=True)
			try:
				for line_number, recipe, expected in iter_prediction_fixture(path):
					try:
						result = PotionPredictor(
							databases[caco_enabled], fixture_settings(mode)
						).evaluate(recipe)
					except (KeyError, ValueError) as error:
						progress.clear()
						if write_log:
							log_prediction_mismatch(
								mode,
								line_number,
								recipe,
								expected,
								f"error: {error}",
								str(error),
								prediction_csv=path,
							)
						print(
							f"First wrong prediction: mode={mode} row={line_number} "
							f"predicted_csv={path} recipe={', '.join(recipe)!r} "
							f"expected={expected} error={error}",
							file=sys.stderr,
						)
						return 1
					actual = result.displayed_value if result.valid else None
					if actual != expected:
						if write_log:
							log_prediction_mismatch(
								mode,
								line_number,
								recipe,
								expected,
								actual,
								prediction_csv=path,
							)
						if (
							actual is not None
							and abs(actual - expected) <= TOLERATED_PREDICTION_DIFFERENCE
						):
							checked += 1
							progress.update(
								f"Checking {mode}: row {line_number} ({checked} predictions)"
							)
							continue
						progress.clear()
						print(
							f"First wrong prediction: mode={mode} row={line_number} "
							f"predicted_csv={path} recipe={', '.join(recipe)!r} "
							f"expected={expected} "
							f"actual={actual}",
							file=sys.stderr,
						)
						return 1
					checked += 1
					progress.update(
						f"Checking {mode}: row {line_number} ({checked} predictions)"
					)
			except (OSError, ValueError) as error:
				progress.clear()
				print(f"Prediction fixture check failed for {path}: {error}", file=sys.stderr)
				return 2
			progress.clear()
			print(f"Passed {mode}: {checked} predictions", flush=True)
		return 0
	finally:
		progress.clear()


def parity_settings(mode: str, perk: str) -> PredictionSettings:
	caco_enabled = mode.startswith("caco")
	alchemy_plus_enabled = "alchemy-plus" in mode
	player = PlayerSettings(
		alchemy_level=15.0,
		**PARITY_PERKS[perk],
		caco_seeker_multiplier=1.05 if caco_enabled else None,
		caco_benefactor_multiplier=1.20 if caco_enabled else None,
	)
	return PredictionSettings(
		caco_enabled=caco_enabled,
		alchemy_plus_enabled=alchemy_plus_enabled,
		player=player,
		caco_ingredient_init_multiplier=4.0,
		caco_skill_factor=1.0 if caco_enabled else 1.5,
		alchemy_plus=AlchemyPlusSettings(
			rounding_enabled=alchemy_plus_enabled,
			impure_cost_fix_enabled=alchemy_plus_enabled,
			magnitude_threshold=25.0,
			magnitude_multiple=5.0,
			duration_threshold=15.0,
			duration_multiple=5.0,
		),
	)


def fixture_settings(mode: str) -> PredictionSettings:
	settings = parity_settings(mode, "none")
	if settings.caco_enabled:
		settings.caco_ingredient_init_multiplier = 3.9
	return settings


class PredictionSelfTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls) -> None:
		cls.vanilla = IngredientDatabase.load(VANILLA_CSV)
		cls.caco = IngredientDatabase.load(CACO_CSV)
		all_recipes = PARITY_RECIPES.values()
		cls.predicted = {
			mode: load_prediction_fixture(path, all_recipes)
			for mode, path in PREDICTION_CSVS.items()
		}

	def test_both_csv_files_load(self) -> None:
		self.assertGreater(len(self.vanilla.names()), 100)
		self.assertGreater(len(self.caco.names()), 100)

	def test_rich_export_metadata_is_loaded(self) -> None:
		columns = sorted(
			IngredientDatabase.REQUIRED_COLUMNS
			| {
				"effect_cost",
				"duration_based",
				"keyword_editor_ids",
				"keyword_form_ids",
				"source_effect_form_id",
				"resolved_effect_name",
				"resolved_effect_form_id",
				"resolved_effect_editor_id",
				"resolved_base_cost",
				"resolved_power_affects_magnitude",
				"resolved_power_affects_duration",
				"resolved_no_magnitude",
				"resolved_no_duration",
				"resolved_beneficial",
				"resolved_harmful",
				"resolved_hostile",
				"resolved_duration_based",
				"resolved_keyword_editor_ids",
				"resolved_keyword_form_ids",
				"resolved_description",
			}
		)
		row = {column: "" for column in columns}
		row.update(
			{
				"ingredient_name": "Exported Ingredient",
				"form_id": "0x1",
				"effect_name": "Source Effect",
				"effect_form_id": "0x100",
				"base_cost": "10",
				"magnitude": "2",
				"duration": "30",
				"power_affects_magnitude": "1",
				"power_affects_duration": "1",
				"no_magnitude": "0",
				"no_duration": "0",
				"beneficial": "1",
				"harmful": "0",
				"hostile": "0",
				"effect_cost": "3.5",
				"duration_based": "1",
				"keyword_editor_ids": "MagicAlchDurationBased;MagicAlchRestoreHealth",
				"keyword_form_ids": "0x200;0x201",
				"source_effect_form_id": "0x100",
				"resolved_effect_name": "Resolved Effect",
				"resolved_effect_form_id": "0x300",
				"resolved_effect_editor_id": "ResolvedEffect",
				"resolved_base_cost": "11",
				"resolved_power_affects_magnitude": "1",
				"resolved_power_affects_duration": "1",
				"resolved_no_magnitude": "0",
				"resolved_no_duration": "0",
				"resolved_beneficial": "1",
				"resolved_harmful": "0",
				"resolved_hostile": "0",
				"resolved_duration_based": "1",
				"resolved_keyword_editor_ids": "MagicAlchDurationBased",
				"resolved_keyword_form_ids": "0x200",
				"resolved_description": "<mag> for <dur> seconds",
			}
		)
		with tempfile.TemporaryDirectory() as temporary:
			path = Path(temporary) / "rich.csv"
			with path.open("w", encoding="utf-8", newline="") as handle:
				writer = csv.DictWriter(handle, fieldnames=columns)
				writer.writeheader()
				writer.writerow(row)
			database = IngredientDatabase.load(path)
			effect = database.get("Exported Ingredient").effects[0]
			self.assertEqual(effect.source_identity, 0x100)
			self.assertEqual(effect.resolved_effect_form_id, 0x300)
			self.assertEqual(effect.keyword_form_ids, (0x200, 0x201))
			self.assertTrue(effect.duration_based)

	def test_current_export_loads_duration_metadata(self) -> None:
		database = IngredientDatabase.load(CACO_CSV)
		self.assertTrue(
			any(effect.duration_based for effect in database.get("Argonian Scales").effects)
		)

	def test_export_without_rich_metadata_is_rejected(self) -> None:
		with tempfile.TemporaryDirectory() as temporary:
			path = Path(temporary) / "outdated.csv"
			columns = sorted(IngredientDatabase.REQUIRED_COLUMNS - {"duration_based"})
			with path.open("w", encoding="utf-8", newline="") as handle:
				csv.DictWriter(handle, fieldnames=columns).writeheader()
			with self.assertRaisesRegex(ValueError, "missing required columns"):
				IngredientDatabase.load(path)

	def test_prediction_export_diagnostic_columns_are_ignored(self) -> None:
		with tempfile.TemporaryDirectory() as temporary:
			path = Path(temporary) / "predictions.csv"
			with path.open("w", encoding="utf-8", newline="") as handle:
				writer = csv.writer(handle)
				writer.writerow(
					[
						"ingredients",
						"predicted_value",
						"name",
						"calculation_details",
					]
				)
				writer.writerow(["A, B", "42", "Potion", "details"])
			self.assertEqual(
				load_prediction_fixture(path, (("A", "B"),)),
				{("A", "B"): 42},
			)

	def test_prediction_fixture_preserves_distinct_same_name_forms(self) -> None:
		expected_recipe = {
			"Abecean Longfin@0x106E1B",
			"Salt Pile@0x34CDF",
			"Salt Pile@0x74A19",
		}
		line_number, recipe, expected = next(
			fixture
			for fixture in iter_prediction_fixture(PREDICTION_CSVS["vanilla"])
			if set(fixture[1]) == expected_recipe
		)
		self.assertEqual(line_number, 1584)
		self.assertEqual(expected, 420)
		result = PotionPredictor(self.vanilla, fixture_settings("vanilla")).evaluate(
			recipe
		)
		self.assertTrue(result.valid)
		self.assertEqual(result.displayed_value, expected)

	def test_prediction_mismatch_log_is_newest_first(self) -> None:
		with tempfile.TemporaryDirectory() as temporary:
			path = Path(temporary) / "potion_prediction_test.log"
			log_prediction_mismatch(
				"vanilla",
				12,
				("A", "B"),
				139,
				56,
				path=path,
				prediction_csv=Path("potions-predicted-vanilla.csv"),
			)
			log_prediction_mismatch(
				"caco+alchemy-plus",
				18,
				("A", "B", "C"),
				22,
				17,
				path=path,
				prediction_csv=Path("potions-predicted-caco-ap.csv"),
			)
			contents = path.read_text(encoding="utf-8")
			self.assertLess(
				contents.index("Fixture mode: caco+alchemy-plus"),
				contents.index("Fixture mode: vanilla"),
			)
			self.assertIn("Report time: ", contents)
			self.assertIn("Enabled plugin set: CACO and Alchemy Plus enabled", contents)
			self.assertIn("Predicted CSV: potions-predicted-caco-ap.csv", contents)
			self.assertIn("Expected value from predicted CSV: 22", contents)
			self.assertIn("Python predicted: 17", contents)

	def test_all_four_paths_are_selectable(self) -> None:
		recipe = ("Abecean Longfin", "Beehive Husk")
		for caco_enabled in (False, True):
			database = self.caco if caco_enabled else self.vanilla
			for alchemy_plus_enabled in (False, True):
				settings = PredictionSettings(
					caco_enabled=caco_enabled,
					alchemy_plus_enabled=alchemy_plus_enabled,
				)
				result = PotionPredictor(database, settings).evaluate(recipe)
				self.assertTrue(result.valid)
				self.assertGreater(result.cost, 0.0)

	def test_shared_effects_are_selected_once(self) -> None:
		settings = PredictionSettings(False, False)
		result = PotionPredictor(self.vanilla, settings).evaluate(
			("Abecean Longfin", "Ash Hopper Jelly")
		)
		identities = [effect.source.source_identity for effect in result.effects]
		self.assertEqual(len(identities), len(set(identities)))

	def test_cli_defaults_match_reported_caco_recipe(self) -> None:
		arguments = make_parser().parse_args(
			[
				"Abecean Longfin",
				"Alocasia Fruit",
				"Canis Root",
				"--caco-enabled",
				"true",
				"--alchemy-plus-enabled",
				"false",
			]
		)
		settings = build_settings(arguments)
		self.assertEqual(settings.player.alchemy_level, 15.0)
		self.assertAlmostEqual(settings.caco_ingredient_init_multiplier, 3.9, places=6)
		self.assertAlmostEqual(settings.caco_skill_factor, 1.0, places=6)
		result = PotionPredictor(self.caco, settings).evaluate(arguments.ingredients)
		self.assertEqual(result.displayed_value, 56)

	def test_prediction_log_flag_defaults_to_enabled(self) -> None:
		self.assertTrue(make_parser().parse_args([]).prediction_log)
		self.assertFalse(
			make_parser().parse_args(["--no-prediction-log"]).prediction_log
		)

	def test_caco_physician_uses_caco_perk_behavior(self) -> None:
		recipe = ("Crushed Amber", "Ironwood Extract", "Red Mountain Flower Extract")
		settings = PredictionSettings(
			caco_enabled=True,
			alchemy_plus_enabled=False,
			player=PlayerSettings(alchemy_level=15.0, physician=True),
		)
		result = PotionPredictor(self.caco, settings).evaluate(recipe)
		self.assertTrue(result.valid)
		self.assertEqual(result.displayed_value, 87)

	def test_caco_benefactor_does_not_apply_to_mixed_potion(self) -> None:
		recipe = ("Elven Heart", "Honeycomb", "Ironwood Extract")
		settings = PredictionSettings(
			caco_enabled=True,
			alchemy_plus_enabled=False,
			player=PlayerSettings(alchemy_level=15.0, benefactor=True),
		)
		result = PotionPredictor(self.caco, settings).evaluate(recipe)
		self.assertTrue(result.valid)
		self.assertTrue(result.has_beneficial)
		self.assertTrue(result.has_harmful)
		self.assertEqual(result.displayed_value, 135)

	def test_caco_duration_based_effect_does_not_discard_duration_flag(self) -> None:
		database = self.caco
		silence = next(
			effect
			for effect in database.get("Alocasia Fruit").effects
			if effect.effect_name == "Silence"
		)
		forced_no_duration = replace(silence, no_duration=True)
		settings = fixture_settings("caco")
		magnitude_factor, duration_factor = effect_power_factors(
			forced_no_duration,
			settings.player,
			potion=False,
			include_type_perks=True,
			caco_enabled=True,
			caco_ingredient_init_multiplier=settings.caco_ingredient_init_multiplier,
			caco_skill_factor=settings.caco_skill_factor,
		)
		magnitude, duration = calculate_effect_input(
			forced_no_duration, magnitude_factor, duration_factor
		)
		self.assertEqual(duration, 3.0)
		self.assertAlmostEqual(
			effect_cost_precise(forced_no_duration, magnitude, duration),
			30.0546603,
			places=6,
		)

	def test_caco_duration_only_effect_uses_duration_power_factor(self) -> None:
		result = PotionPredictor(
			self.caco, fixture_settings("caco")
		).evaluate(("Abecean Longfin", "Argonian Scales", "Bergamot Seeds"))
		self.assertEqual(result.displayed_value, 78)

	def test_caco_slow_effect_uses_duration_power_factor(self) -> None:
		result = PotionPredictor(
			self.caco, fixture_settings("caco")
		).evaluate(("Abecean Longfin", "Arrowroot", "Aster Bloom Core"))
		self.assertEqual(result.displayed_value, 58)

	def test_alchemy_plus_rounding_matches_native_order(self) -> None:
		self.assertEqual(apply_alchemy_plus_rounding(9.4, 10.0, 1.0), 9.4)
		self.assertEqual(apply_alchemy_plus_rounding(10.4, 10.0, 1.0), 11.0)

	def test_impure_signed_adjustment(self) -> None:
		adjusted, impure = adjust_impure_effect_cost(12.0, True, False, True)
		self.assertTrue(impure)
		self.assertEqual(adjusted, -12.0)
		adjusted, impure = adjust_impure_effect_cost(12.0, True, True, True)
		self.assertFalse(impure)
		self.assertEqual(adjusted, 12.0)


def make_parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(
		description="Predict a two- or three-ingredient Skyrim potion value using the plugin algorithm."
	)
	parser.add_argument(
		"ingredients",
		nargs="*",
		help="two or three ingredient names; quote names containing spaces",
	)
	parser.add_argument(
		"--caco-enabled",
		type=parse_bool,
		default=None,
		metavar="BOOL",
		help="enable the CACO path (true/false); required for a prediction",
	)
	parser.add_argument(
		"--alchemy-plus-enabled",
		type=parse_bool,
		default=None,
		metavar="BOOL",
		help="enable the Alchemy Plus path (true/false); required for a prediction",
	)
	parser.add_argument(
		"--alchemy-plus-config",
		type=Path,
		help="optional AlchemyPlus.json; otherwise both supported features use deterministic defaults",
	)
	parser.add_argument(
		"--vanilla-csv",
		type=Path,
		default=VANILLA_CSV,
		help=f"vanilla CSV (default: {VANILLA_CSV})",
	)
	parser.add_argument(
		"--caco-csv",
		type=Path,
		default=CACO_CSV,
		help=f"CACO CSV (default: {CACO_CSV})",
	)
	parser.add_argument(
		"--alchemy-level",
		type=float,
		default=15.0,
		help="player Alchemy actor value (default: 15)",
	)
	parser.add_argument(
		"--fortify-alchemy",
		type=float,
		default=0.0,
		help="Fortify Alchemy actor-value bonus (default: 0)",
	)
	parser.add_argument(
		"--alchemist-perk-rank",
		type=int,
		default=0,
		choices=range(0, 6),
		help="vanilla Alchemist rank 0-5 (default: 0)",
	)
	parser.add_argument(
		"--alchemist-multiplier",
		type=float,
		default=None,
		help="explicit fallback Alchemist multiplier; overrides rank fallback",
	)
	parser.add_argument("--purity", action=argparse.BooleanOptionalAction, default=False)
	parser.add_argument("--physician", action=argparse.BooleanOptionalAction, default=False)
	parser.add_argument("--benefactor", action=argparse.BooleanOptionalAction, default=False)
	parser.add_argument("--poisoner", action=argparse.BooleanOptionalAction, default=False)
	parser.add_argument(
		"--seeker-of-shadows",
		action=argparse.BooleanOptionalAction,
		default=False,
	)
	parser.add_argument(
		"--caco-ingredient-init",
		type=float,
		default=3.9,
		help="CACO fAlchemyIngredientInitMult (default: 3.9)",
	)
	parser.add_argument(
		"--caco-skill-factor",
		type=float,
		default=1.0,
		help="CACO fAlchemySkillFactor (default: 1.0)",
	)
	for option, destination, help_text in (
		(
			"--caco-physician-multiplier",
			"caco_physician_multiplier",
			"CACO-native Physician perk-entry multiplier",
		),
		(
			"--caco-benefactor-multiplier",
			"caco_benefactor_multiplier",
			"CACO-native Benefactor perk-entry multiplier",
		),
		(
			"--caco-poisoner-multiplier",
			"caco_poisoner_multiplier",
			"CACO-native Poisoner perk-entry multiplier",
		),
		(
			"--caco-seeker-multiplier",
			"caco_seeker_multiplier",
			"CACO-native Seeker of Shadows perk-entry multiplier",
		),
	):
		parser.add_argument(option, dest=destination, type=float, default=None, help=help_text)
	parser.add_argument(
		"--caco-impure-processing",
		type=parse_bool,
		default=None,
		metavar="BOOL",
		help="enable CACO's optional 20%% impure-potion adjustment (default: false)",
	)
	parser.add_argument(
		"--alchemy-plus-rounding",
		type=parse_bool,
		default=None,
		metavar="BOOL",
		help="override roundedPotency.enabled (default: config value or true when AP is enabled)",
	)
	parser.add_argument(
		"--alchemy-plus-impure-cost-fix",
		type=parse_bool,
		default=None,
		metavar="BOOL",
		help="override impureCostFix.enabled (default: config value or true when AP is enabled)",
	)
	parser.add_argument("--ap-magnitude-threshold", type=float, default=None)
	parser.add_argument("--ap-magnitude-multiple", type=float, default=None)
	parser.add_argument("--ap-duration-threshold", type=float, default=None)
	parser.add_argument("--ap-duration-multiple", type=float, default=None)
	parser.add_argument(
		"--list-ingredients",
		action="store_true",
		help="list ingredient names from the selected data source and exit",
	)
	parser.add_argument(
		"--self-test",
		action="store_true",
		help="run built-in regression tests and exit",
	)
	parser.add_argument(
		"--check-predicted-csvs",
		action="store_true",
		help="check all predicted potion CSVs in fixture order and stop at the first mismatch",
	)
	parser.add_argument(
		"--prediction-log",
		action=argparse.BooleanOptionalAction,
		default=True,
		help="write prediction mismatches to potion_prediction_test.log (default: enabled)",
	)
	parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
	return parser


def main(argv: Sequence[str] | None = None) -> int:
	parser = make_parser()
	arguments = parser.parse_args(argv)

	if arguments.self_test:
		suite = unittest.defaultTestLoader.loadTestsFromTestCase(PredictionSelfTests)
		result = unittest.TextTestRunner(verbosity=2).run(suite)
		return 0 if result.wasSuccessful() else 1
	if arguments.check_predicted_csvs:
		return run_prediction_fixture_check(
			arguments.vanilla_csv,
			arguments.caco_csv,
			write_log=arguments.prediction_log,
		)

	if arguments.caco_enabled is None or arguments.alchemy_plus_enabled is None:
		parser.error(
			"--caco-enabled and --alchemy-plus-enabled are required unless --self-test is used"
		)
	if arguments.list_ingredients:
		database = IngredientDatabase.load(
			arguments.caco_csv if arguments.caco_enabled else arguments.vanilla_csv
		)
		print("\n".join(database.names()))
		return 0
	if len(arguments.ingredients) not in {2, 3}:
		parser.error("provide exactly two or three ingredient names")

	database = IngredientDatabase.load(
		arguments.caco_csv if arguments.caco_enabled else arguments.vanilla_csv
	)
	settings = build_settings(arguments)
	result = PotionPredictor(database, settings).evaluate(arguments.ingredients)
	if not result.valid:
		print("The recipe has no shared effects and cannot produce a potion.", file=sys.stderr)
		return 2
	print(result_json(result, database) if arguments.json else format_result(result, database))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
