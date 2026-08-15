#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>

#include <base/TimeAutomation.h>

#include <cstdlib>
#include <iostream>

namespace
{
	void expect(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << message << '\n';
			std::exit(1);
		}
	}

	QJsonObject rule(const QString& id, const QString& start, const QString& end, QJsonObject values, int priority = 0)
	{
		return {
			{ "id", id },
			{ "enabled", true },
			{ "priority", priority },
			{ "conditions", QJsonArray{ QJsonObject{ { "type", "time-range" }, { "start", start }, { "end", end } } } },
			{ "actions", QJsonArray{ QJsonObject{ { "type", "adjustment" }, { "values", values } } } }
		};
	}

	QJsonObject rule(const QString& id, const QString& start, const QString& end, double scaleOutput, int priority = 0)
	{
		return rule(id, start, end, QJsonObject{ { "scaleOutput", scaleOutput } }, priority);
	}

	QJsonObject config(QJsonArray rules)
	{
		return {
			{ "version", 1 },
			{ "enabled", true },
			{ "timezone", "UTC" },
			{ "rules", rules }
		};
	}

	QDateTime utc(const QDate& date, const QTime& time)
	{
		return QDateTime(date, time, QTimeZone::utc());
	}
}

int main()
{
	QString error;
	const QJsonObject daytime = config(QJsonArray{ rule("day", "06:00", "18:00", 0.8) });
	expect(TimeAutomation::validate(daytime, error), "valid daytime rule rejected");

	auto result = TimeAutomation::evaluate(daytime, utc(QDate(2026, 1, 1), QTime(12, 0)));
	expect(result.activeRuleIds == QStringList{ "day" }, "midday must reconcile the daytime rule without a boundary event");
	expect(result.overlay.value("scaleOutput").toDouble() == 0.8, "midday scaleOutput must be 0.8");
	expect(result.nextTransition && *result.nextTransition == utc(QDate(2026, 1, 1), QTime(18, 0)), "midday next transition must be 18:00");

	const QJsonObject night = config(QJsonArray{ rule("night", "18:00", "06:00", 0.1) });
	result = TimeAutomation::evaluate(night, utc(QDate(2026, 1, 2), QTime(0, 30)));
	expect(result.activeRuleIds == QStringList{ "night" }, "overnight rule must be active at 00:30");
	expect(result.overlay.value("scaleOutput").toDouble() == 0.1, "overnight scaleOutput must be 0.1");
	expect(result.nextTransition && *result.nextTransition == utc(QDate(2026, 1, 2), QTime(6, 0)), "overnight next transition must be 06:00");
	expect(TimeAutomation::evaluate(daytime, utc(QDate(2026, 1, 1), QTime(18, 0))).overlay.isEmpty(), "end must be exclusive");
	expect(TimeAutomation::evaluate(daytime, utc(QDate(2026, 1, 1), QTime(6, 0))).overlay.value("scaleOutput").toDouble() == 0.8, "start must be inclusive");

	result = TimeAutomation::evaluate(config(QJsonArray{ rule("low", "00:00", "23:59", 0.2, 1), rule("high", "00:00", "23:59", 0.7, 2) }), utc(QDate(2026, 1, 1), QTime(12, 0)));
	expect(result.overlay.value("scaleOutput").toDouble() == 0.7, "higher priority must win per field");
	result = TimeAutomation::evaluate(config(QJsonArray{ rule("scale", "00:00", "23:59", QJsonObject{ { "scaleOutput", 0.7 } }), rule("gamma", "00:00", "23:59", QJsonObject{ { "gamma", 2.0 } }) }), utc(QDate(2026, 1, 1), QTime(12, 0)));
	expect(result.overlay.value("scaleOutput").toDouble() == 0.7 && result.overlay.value("gamma").toDouble() == 2.0, "overlapping rules must merge different adjustment fields");
	result = TimeAutomation::evaluate(config(QJsonArray{ rule("first", "00:00", "23:59", 0.2, 2), rule("second", "00:00", "23:59", 0.7, 2) }), utc(QDate(2026, 1, 1), QTime(12, 0)));
	expect(result.overlay.value("scaleOutput").toDouble() == 0.7, "later equal-priority rule must win deterministically");
	QJsonObject disabled = daytime;
	disabled["enabled"] = false;
	expect(TimeAutomation::evaluate(disabled, utc(QDate(2026, 1, 1), QTime(12, 0))).overlay.isEmpty(), "disabled engine must contribute no overlay");

	QJsonObject invalid = config(QJsonArray{ rule("same", "06:00", "06:00", 0.8) });
	expect(!TimeAutomation::validate(invalid, error), "equal range must be rejected");
	invalid = config(QJsonArray{ rule("duplicate", "06:00", "18:00", 0.8), rule("duplicate", "18:00", "06:00", 0.1) });
	expect(!TimeAutomation::validate(invalid, error), "duplicate ids must be rejected");
	invalid = daytime;
	invalid["timezone"] = "Mars/Olympus";
	expect(!TimeAutomation::validate(invalid, error), "invalid timezone must be rejected");
	invalid = daytime;
	QJsonObject invalidRule = invalid["rules"].toArray().first().toObject();
	invalidRule["conditions"] = QJsonArray{ QJsonObject{ { "type", "solar" }, { "start", "06:00" }, { "end", "18:00" } } };
	invalid["rules"] = QJsonArray{ invalidRule };
	expect(!TimeAutomation::validate(invalid, error), "unsupported condition must be rejected");
	invalid = daytime;
	invalidRule = invalid["rules"].toArray().first().toObject();
	invalidRule["actions"] = QJsonArray{ QJsonObject{ { "type", "adjustment" }, { "values", QJsonObject{ { "scaleOutput", 3.0 } } } } };
	invalid["rules"] = QJsonArray{ invalidRule };
	expect(!TimeAutomation::validate(invalid, error), "invalid adjustment range must be rejected");
	expect(TimeAutomation::validate(daytime, error) && error.isEmpty(), "validation must clear stale errors");

	QJsonObject london = daytime;
	london["timezone"] = "Europe/London";
	result = TimeAutomation::evaluate(london, utc(QDate(2026, 3, 29), QTime(0, 30)));
	expect(result.nextTransition && result.nextTransition->isValid() && *result.nextTransition > result.evaluatedAt, "DST forward next transition must be valid and future");
	result = TimeAutomation::evaluate(london, utc(QDate(2026, 10, 25), QTime(0, 30)));
	expect(result.nextTransition && result.nextTransition->isValid() && *result.nextTransition > result.evaluatedAt, "DST backward next transition must be valid and future");

	const QJsonObject baseline{ { "scaleOutput", 1.0 }, { "gamma", 1.5 } };
	const QJsonObject merged = TimeAutomation::merge(baseline, QJsonObject{ { "scaleOutput", 0.8 } });
	expect(merged.value("scaleOutput").toDouble() == 0.8 && merged.value("gamma").toDouble() == 1.5, "overlay must preserve the manual baseline for non-overridden fields");
	expect(TimeAutomation::merge(baseline, {}).value("scaleOutput").toDouble() == 1.0, "clearing an overlay must reveal the latest baseline supplied to merge");
	const QJsonObject replacementBaseline{ { "scaleOutput", 0.5 }, { "gamma", 2.0 } };
	expect(TimeAutomation::merge(replacementBaseline, QJsonObject{ { "scaleOutput", 0.8 } }).value("gamma").toDouble() == 2.0, "a persisted baseline replacement must retain the active overlay");
	expect(TimeAutomation::merge(replacementBaseline, {}).value("scaleOutput").toDouble() == 0.5, "removing an overlay must reveal a replaced baseline");

	return 0;
}
