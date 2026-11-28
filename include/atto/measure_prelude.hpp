#pragma once
#include <string_view>
namespace atto::forge::scalar {
// Exact compiler-owned common-source #164 bytes.
// SHA256 a411966eb3188104cd13cc2201b4d1d0de1e12404378aba313b396ae726a3376.
inline constexpr std::string_view measure_source=R"atto_measure(// Portable core reference prelude; file-local Verified Core candidate.
// Import/export and generic refinements are not yet admitted by this profile.
define dimension Length;
define unit Meter: Length;
define unit Millimeter: Length = Meter / 1000;
define unit Centimeter: Length = Meter / 100;
define unit Kilometer: Length = Meter * 1000;
define unit Micrometer: Length = Meter / 1000000;
define unit Nanometer: Length = Meter / 1000000000;
// Complete SI decimal length prefix family; scales remain exact rational.
define unit Decameter: Length = Meter * 10;
define unit Hectometer: Length = Decameter * 10;
define unit Megameter: Length = Kilometer * 1000;
define unit Gigameter: Length = Megameter * 1000;
define unit Terameter: Length = Gigameter * 1000;
define unit Petameter: Length = Terameter * 1000;
define unit Exameter: Length = Petameter * 1000;
define unit Zettameter: Length = Exameter * 1000;
define unit Yottameter: Length = Zettameter * 1000;
define unit Ronnameter: Length = Yottameter * 1000;
define unit Quettameter: Length = Ronnameter * 1000;
define unit Decimeter: Length = Meter / 10;
define unit Picometer: Length = Nanometer / 1000;
define unit Femtometer: Length = Picometer / 1000;
define unit Attometer: Length = Femtometer / 1000;
define unit Zeptometer: Length = Attometer / 1000;
define unit Yoctometer: Length = Zeptometer / 1000;
define unit Rontometer: Length = Yoctometer / 1000;
define unit Quectometer: Length = Rontometer / 1000;
define dimension Time;
define unit Second: Time;
define unit Millis: Time = Second / 1000;
define unit Micros: Time = Second / 1000000;
define unit Nanos: Time = Second / 1000000000;
define unit Minute: Time = Second * 60;
define unit Hour: Time = Second * 3600;
define unit Day: Time = Second * 86400;
define dimension Mass;
define unit Kilogram: Mass;
define unit Gram: Mass = Kilogram / 1000;
define unit Milligram: Mass = Gram / 1000;
define dimension ElectricCurrent;
define unit Ampere: ElectricCurrent;
define unit Milliampere: ElectricCurrent = Ampere / 1000;
define dimension Temperature;
// Linear temperature differences only; absolute points are separately gated.
define unit Kelvin: Temperature;
define dimension AmountOfSubstance;
define unit Mole: AmountOfSubstance;
define dimension LuminousIntensity;
define unit Candela: LuminousIntensity;
define dimension Information;
define unit Bit: Information;
define unit Byte: Information = Bit * 8;
define unit Kilobyte: Information = Byte * 1000;
define unit Megabyte: Information = Byte * 1000000;
define unit Gigabyte: Information = Byte * 1000000000;
define unit Kibibyte: Information = Byte * 1024;
define unit Mebibyte: Information = Byte * 1048576;
define unit Gibibyte: Information = Byte * 1073741824;

type PositiveInt = int where self > 0;
type NegativeInt = int where self < 0;
type NonNegativeInt = int where self >= 0;
type NonPositiveInt = int where self <= 0;
type PositiveI32 = i32 where self > 0;
type NegativeI32 = i32 where self < 0;
type NonNegativeI32 = i32 where self >= 0;
type NonPositiveI32 = i32 where self <= 0;
)atto_measure";
inline constexpr std::string_view measure_units[]={"Meter","Millimeter","Centimeter","Kilometer","Micrometer","Nanometer","Decameter","Hectometer","Megameter","Gigameter","Terameter","Petameter","Exameter","Zettameter","Yottameter","Ronnameter","Quettameter","Decimeter","Picometer","Femtometer","Attometer","Zeptometer","Yoctometer","Rontometer","Quectometer","Second","Millis","Micros","Nanos","Minute","Hour","Day","Kilogram","Gram","Milligram","Ampere","Milliampere","Kelvin","Mole","Candela","Bit","Byte","Kilobyte","Megabyte","Gigabyte","Kibibyte","Mebibyte","Gibibyte"};
inline constexpr std::string_view measure_aliases[]={"PositiveInt","NegativeInt","NonNegativeInt","NonPositiveInt","PositiveI32","NegativeI32","NonNegativeI32","NonPositiveI32"};
}
