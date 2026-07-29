// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstdint>

namespace ABTS::M11Core
{
	inline constexpr std::int32_t InvalidIndex = -1;

	// These constants intentionally preserve Unreal's literal types before
	// promotion to double. TVector<double> uses the float UE_SMALL_NUMBER and
	// UE_KINDA_SMALL_NUMBER defaults, while solver code explicitly uses the
	// double constant where noted.
	inline constexpr double SmallNumber = static_cast<double>(1.0e-8f);
	inline constexpr double KindaSmallNumber = static_cast<double>(1.0e-4f);
	inline constexpr double DoubleSmallNumber = 1.0e-8;

	struct Vec3d
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;

		constexpr Vec3d() = default;
		constexpr Vec3d(
			const double InX,
			const double InY,
			const double InZ)
			: X(InX)
			, Y(InY)
			, Z(InZ)
		{
		}

		[[nodiscard]] double SquaredLength() const
		{
			return X * X + Y * Y + Z * Z;
		}

		[[nodiscard]] double Length() const
		{
			return std::sqrt(X * X + Y * Y + Z * Z);
		}

		[[nodiscard]] bool IsNearlyZero(
			const double Tolerance = KindaSmallNumber) const
		{
			return std::abs(X) <= Tolerance
				&& std::abs(Y) <= Tolerance
				&& std::abs(Z) <= Tolerance;
		}

		bool Normalize(const double Tolerance = SmallNumber)
		{
			const double SquareSum = X * X + Y * Y + Z * Z;
			if (SquareSum > Tolerance)
			{
				const double Scale = 1.0 / std::sqrt(SquareSum);
				X *= Scale;
				Y *= Scale;
				Z *= Scale;
				return true;
			}
			return false;
		}

		[[nodiscard]] Vec3d GetSafeNormal(
			const double Tolerance = SmallNumber,
			const Vec3d& ResultIfZero = Vec3d()) const
		{
			const double SquareSum = X * X + Y * Y + Z * Z;
			if (SquareSum == 1.0)
			{
				return *this;
			}
			if (SquareSum < Tolerance)
			{
				return ResultIfZero;
			}
			const double Scale = 1.0 / std::sqrt(SquareSum);
			return Vec3d(X * Scale, Y * Scale, Z * Scale);
		}

		Vec3d& operator+=(const Vec3d& Other)
		{
			X += Other.X;
			Y += Other.Y;
			Z += Other.Z;
			return *this;
		}

		Vec3d& operator-=(const Vec3d& Other)
		{
			X -= Other.X;
			Y -= Other.Y;
			Z -= Other.Z;
			return *this;
		}

		Vec3d& operator*=(const double Scale)
		{
			X *= Scale;
			Y *= Scale;
			Z *= Scale;
			return *this;
		}

		Vec3d& operator/=(const double Scale)
		{
			const double ReciprocalScale = 1.0 / Scale;
			X *= ReciprocalScale;
			Y *= ReciprocalScale;
			Z *= ReciprocalScale;
			return *this;
		}

		[[nodiscard]] Vec3d operator-() const
		{
			return Vec3d(-X, -Y, -Z);
		}

		[[nodiscard]] static double DotProduct(
			const Vec3d& Left,
			const Vec3d& Right)
		{
			return Left.X * Right.X
				+ Left.Y * Right.Y
				+ Left.Z * Right.Z;
		}

		[[nodiscard]] static Vec3d CrossProduct(
			const Vec3d& Left,
			const Vec3d& Right)
		{
			return Vec3d(
				Left.Y * Right.Z - Left.Z * Right.Y,
				Left.Z * Right.X - Left.X * Right.Z,
				Left.X * Right.Y - Left.Y * Right.X);
		}
	};

	[[nodiscard]] inline Vec3d operator+(Vec3d Left, const Vec3d& Right)
	{
		Left += Right;
		return Left;
	}

	[[nodiscard]] inline Vec3d operator-(Vec3d Left, const Vec3d& Right)
	{
		Left -= Right;
		return Left;
	}

	[[nodiscard]] inline Vec3d operator*(Vec3d Value, const double Scale)
	{
		Value *= Scale;
		return Value;
	}

	[[nodiscard]] inline Vec3d operator*(const double Scale, Vec3d Value)
	{
		Value *= Scale;
		return Value;
	}

	[[nodiscard]] inline Vec3d operator/(Vec3d Value, const double Scale)
	{
		// TVector<double>::operator/ computes one reciprocal and multiplies
		// every component. Preserve that operation order for bit parity.
		const double ReciprocalScale = 1.0 / Scale;
		return Vec3d(
			Value.X * ReciprocalScale,
			Value.Y * ReciprocalScale,
			Value.Z * ReciprocalScale);
	}

	[[nodiscard]] inline bool operator==(
		const Vec3d& Left,
		const Vec3d& Right)
	{
		return Left.X == Right.X
			&& Left.Y == Right.Y
			&& Left.Z == Right.Z;
	}

	struct Color4f
	{
		float R = 1.0f;
		float G = 1.0f;
		float B = 1.0f;
		float A = 1.0f;
	};

	template <typename TValue>
	[[nodiscard]] constexpr TValue Min(
		const TValue A,
		const TValue B)
	{
		// Exact FGenericPlatformMath tie/NaN direction.
		return A < B ? A : B;
	}

	template <typename TValue>
	[[nodiscard]] constexpr TValue Max(
		const TValue A,
		const TValue B)
	{
		// Exact FGenericPlatformMath tie/NaN direction.
		return B < A ? A : B;
	}

	template <typename TValue>
	[[nodiscard]] constexpr TValue Clamp(
		const TValue Value,
		const TValue Minimum,
		const TValue Maximum)
	{
		// Exact FMath expression order.
		return Max(Min(Value, Maximum), Minimum);
	}

	template <typename TValue>
	[[nodiscard]] constexpr TValue Square(const TValue Value)
	{
		return Value * Value;
	}

	[[nodiscard]] inline bool IsFinite(const double Value)
	{
		return std::isfinite(Value);
	}

	[[nodiscard]] inline bool IsFinite(const float Value)
	{
		return std::isfinite(Value);
	}

	[[nodiscard]] inline double Sqrt(const double Value)
	{
		return std::sqrt(Value);
	}

	[[nodiscard]] inline double Pow(
		const double Base,
		const double Exponent)
	{
		return std::pow(Base, Exponent);
	}

	[[nodiscard]] inline double Acos(const double Value)
	{
		return std::acos(Value);
	}

	[[nodiscard]] inline double Asin(const double Value)
	{
		return std::asin(Value);
	}

	[[nodiscard]] constexpr double Lerp(
		const double A,
		const double B,
		const double Alpha)
	{
		return A + Alpha * (B - A);
	}
}
