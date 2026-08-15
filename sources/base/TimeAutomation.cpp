#include <base/TimeAutomation.h>

#ifndef PCH_ENABLED
	#include <QJsonArray>
	#include <QMap>
	#include <QRegularExpression>
	#include <QSet>
	#include <QTimeZone>

	#include <cmath>
#endif

namespace
{
	bool validTime(const QString& value)
	{
		return QRegularExpression("^(?:[01]\\d|2[0-3]):[0-5]\\d$").match(value).hasMatch() && QTime::fromString(value, "HH:mm").isValid();
	}

	bool validNumber(const QJsonValue& value, double minimum, double maximum)
	{
		return value.isDouble() && value.toDouble() >= minimum && value.toDouble() <= maximum;
	}

	bool validColor(const QJsonValue& value)
	{
		if (!value.isArray() || value.toArray().size() != 3)
			return false;

		for (const QJsonValue& component : value.toArray())
		{
			if (!component.isDouble() || std::floor(component.toDouble()) != component.toDouble() || component.toInt() < 0 || component.toInt() > 255)
				return false;
		}
		return true;
	}

	bool validAdjustment(const QJsonObject& values, QString& error)
	{
		static const QSet<QString> colorFields{ "red", "green", "blue", "cyan", "magenta", "yellow", "white", "black" };
		static const QMap<QString, QPair<double, double>> numberFields{
			{ "scaleOutput", { 0.0, 2.0 } }, { "gamma", { 0.1, 5.0 } }, { "backlightThreshold", { 0.0, 1.0 } },
			{ "luminanceGain", { 0.0, 10.0 } }, { "saturationGain", { 0.0, 10.0 } }, { "temperatureRed", { 0.0, 1.0 } },
			{ "temperatureGreen", { 0.0, 1.0 } }, { "temperatureBlue", { 0.0, 1.0 } }, { "powerLimit", { 0.0, 1.0 } }
		};
		static const QSet<QString> allowed{ "classic_config", "backlightColored", "temperatureSetting", "red", "green", "blue", "cyan", "magenta", "yellow", "white", "black", "scaleOutput", "gamma", "backlightThreshold", "luminanceGain", "saturationGain", "temperatureRed", "temperatureGreen", "temperatureBlue", "powerLimit" };

		if (values.isEmpty())
		{
			error = "adjustment values must not be empty";
			return false;
		}

		for (auto it = values.constBegin(); it != values.constEnd(); ++it)
		{
			if (!allowed.contains(it.key()))
			{
				error = "unsupported adjustment value: " + it.key();
				return false;
			}
			if (colorFields.contains(it.key()) && !validColor(it.value()))
			{
				error = "invalid adjustment color: " + it.key();
				return false;
			}
			if (numberFields.contains(it.key()) && !validNumber(it.value(), numberFields[it.key()].first, numberFields[it.key()].second))
			{
				error = "invalid adjustment value: " + it.key();
				return false;
			}
			if ((it.key() == "classic_config" || it.key() == "backlightColored") && !it.value().isBool())
			{
				error = "invalid adjustment value: " + it.key();
				return false;
			}
			if (it.key() == "temperatureSetting" && (!it.value().isString() || !QSet<QString>{ "disabled", "cold", "neutral", "warm", "custom" }.contains(it.value().toString())))
			{
				error = "invalid adjustment value: temperatureSetting";
				return false;
			}
		}
		return true;
	}

	QTimeZone timeZoneFor(const QJsonObject& config)
	{
		const QString id = config.value("timezone").toString();
		return id == "system" ? QTimeZone::systemTimeZone() : QTimeZone(id.toUtf8());
	}

	bool isActive(const QTime& time, const QTime& start, const QTime& end)
	{
		return start < end ? time >= start && time < end : time >= start || time < end;
	}

	QDateTime localTransition(const QDate& date, const QTime& time, const QTimeZone& timezone)
	{
		return QDateTime(date, QTime(0, 0), timezone).addSecs(QTime(0, 0).secsTo(time));
	}
}

bool TimeAutomation::validate(const QJsonObject& config, QString& error)
{
	error.clear();
	static const QSet<QString> engineFields{ "version", "enabled", "timezone", "rules" };
	for (const QString& field : config.keys())
	{
		if (!engineFields.contains(field))
		{
			error = "unsupported automation setting: " + field;
			return false;
		}
	}

	if (config.value("version").toInt(-1) != 1 || !config.value("enabled").isBool() || !config.value("timezone").isString() || !config.value("rules").isArray())
	{
		error = "automation requires version 1, enabled, timezone, and rules";
		return false;
	}

	const QString timezone = config.value("timezone").toString();
	if (timezone != "system" && !QTimeZone(timezone.toUtf8()).isValid())
	{
		error = "invalid timezone: " + timezone;
		return false;
	}

	QSet<QString> ids;
	for (const QJsonValue& value : config.value("rules").toArray())
	{
		if (!value.isObject())
		{
			error = "automation rule must be an object";
			return false;
		}
		const QJsonObject rule = value.toObject();
		for (const QString& field : rule.keys())
		{
			if (!QSet<QString>{ "id", "enabled", "priority", "conditions", "actions" }.contains(field))
			{
				error = "unsupported rule setting: " + field;
				return false;
			}
		}
		const QString id = rule.value("id").toString();
		if (id.isEmpty() || ids.contains(id) || !rule.value("enabled").isBool() || !rule.value("priority").isDouble() || std::floor(rule.value("priority").toDouble()) != rule.value("priority").toDouble())
		{
			error = "automation rules require unique nonempty ids, enabled, and integer priority";
			return false;
		}
		ids.insert(id);

		const QJsonArray conditions = rule.value("conditions").toArray();
		const QJsonArray actions = rule.value("actions").toArray();
		if (conditions.size() != 1 || actions.size() != 1 || !conditions.first().isObject() || !actions.first().isObject())
		{
			error = "automation rules require one condition and one action";
			return false;
		}
		const QJsonObject condition = conditions.first().toObject();
		if (condition.size() != 3 || !condition.contains("type") || !condition.contains("start") || !condition.contains("end") || condition.value("type").toString() != "time-range" || !validTime(condition.value("start").toString()) || !validTime(condition.value("end").toString()) || condition.value("start").toString() == condition.value("end").toString())
		{
			error = "invalid time-range condition";
			return false;
		}
		const QJsonObject action = actions.first().toObject();
		if (action.size() != 2 || !action.contains("type") || !action.contains("values") || action.value("type").toString() != "adjustment" || !action.value("values").isObject() || !validAdjustment(action.value("values").toObject(), error))
		{
			if (error.isEmpty())
				error = "invalid adjustment action";
			return false;
		}
	}
	return true;
}

AutomationEvaluation TimeAutomation::evaluate(const QJsonObject& config, const QDateTime& at)
{
	AutomationEvaluation result;
	const QTimeZone timezone = timeZoneFor(config);
	const QDateTime local = at.toTimeZone(timezone);
	result.evaluatedAt = local;
	if (!config.value("enabled").toBool())
		return result;

	QMap<QString, int> fieldPriorities;
	const QJsonArray rules = config.value("rules").toArray();
	for (const QJsonValue& value : rules)
	{
		const QJsonObject rule = value.toObject();
		if (!rule.value("enabled").toBool())
			continue;

		const QJsonObject condition = rule.value("conditions").toArray().first().toObject();
		const QTime start = QTime::fromString(condition.value("start").toString(), "HH:mm");
		const QTime end = QTime::fromString(condition.value("end").toString(), "HH:mm");
		if (isActive(local.time(), start, end))
		{
			result.activeRuleIds.append(rule.value("id").toString());
			const int priority = rule.value("priority").toInt();
			const QJsonObject values = rule.value("actions").toArray().first().toObject().value("values").toObject();
			for (auto it = values.constBegin(); it != values.constEnd(); ++it)
			{
				if (!fieldPriorities.contains(it.key()) || priority >= fieldPriorities.value(it.key()))
				{
					fieldPriorities[it.key()] = priority;
					result.overlay[it.key()] = it.value();
				}
			}
		}

		for (const QTime transition : { start, end })
		{
			QDateTime candidate = localTransition(local.date(), transition, timezone);
			if (candidate <= local)
				candidate = localTransition(local.date().addDays(1), transition, timezone);
			if (!result.nextTransition || candidate < *result.nextTransition)
				result.nextTransition = candidate;
		}
	}
	return result;
}

QJsonObject TimeAutomation::merge(const QJsonObject& baseline, const QJsonObject& overlay)
{
	QJsonObject effective = baseline;
	for (auto it = overlay.constBegin(); it != overlay.constEnd(); ++it)
		effective[it.key()] = it.value();
	return effective;
}
