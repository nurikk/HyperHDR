#pragma once

#ifndef PCH_ENABLED
	#include <QDateTime>
	#include <QJsonObject>
	#include <QStringList>

	#include <optional>
#endif

struct AutomationEvaluation
{
	QStringList activeRuleIds;
	QJsonObject overlay;
	QDateTime evaluatedAt;
	std::optional<QDateTime> nextTransition;
};

class TimeAutomation
{
public:
	static bool validate(const QJsonObject& config, QString& error);
	static AutomationEvaluation evaluate(const QJsonObject& config, const QDateTime& at);
	static QJsonObject merge(const QJsonObject& baseline, const QJsonObject& overlay);
};
