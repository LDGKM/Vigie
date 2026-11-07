#pragma once

#include <optional>
#include <string>
#include <vector>

namespace vigie::plan {

// Un objectif exprimé en euros d'aujourd'hui, à atteindre dans un nombre d'années donné.
struct Goal {
    std::string name;
    double amount = 0.0;
    int in_years = 1;
};

// Un rendement annuel supposé. C'est une hypothèse de l'utilisateur, pas une prévision.
struct Scenario {
    std::string name;
    double annual_rate = 0.0;  // 0.03 pour 3 %.
};

struct PlanInput {
    std::optional<int> age;
    int horizon_years = 10;
    double monthly_income = 0.0;
    double monthly_expenses = 0.0;
    double current_savings = 0.0;
    std::optional<double> monthly_saving;  // Sans valeur : revenus moins dépenses.
    double saving_growth = 0.0;            // Hausse annuelle de l'épargne mensuelle.
    double inflation = 0.02;
    int emergency_months = 3;
    std::vector<Scenario> scenarios;
    std::vector<Goal> goals;
};

struct YearRow {
    int year = 0;
    std::optional<int> age;
    double contributed = 0.0;               // Versements cumulés, épargne de départ comprise.
    std::vector<double> capital;            // Fin d'année, un montant par scénario.
    double central_in_todays_euros = 0.0;   // Scénario central, corrigé de l'inflation.
    std::vector<std::string> goals_paid;    // Objectifs financés cette année, scénario central.
};

struct GoalResult {
    std::string name;
    int in_years = 0;
    double amount_today = 0.0;
    double amount_then = 0.0;  // Montant augmenté de l'inflation à l'échéance.
    bool reached = false;      // Dans le scénario central.
    double shortfall = 0.0;
};

struct PlanResult {
    double monthly_capacity = 0.0;  // Revenus moins dépenses.
    double monthly_saving = 0.0;
    double saving_rate = 0.0;       // Part des revenus épargnée.
    double emergency_target = 0.0;
    std::optional<int> months_to_emergency;
    std::size_t central = 0;        // Indice du scénario central.
    std::vector<Scenario> scenarios;
    std::vector<YearRow> years;
    std::vector<GoalResult> goals;
    std::optional<double> monthly_saving_for_all_goals;  // Scénario central.
    std::vector<std::string> warnings;
};

// Lève std::invalid_argument si une valeur sort des bornes prévues.
void validate(const PlanInput& input);

// Projection mois par mois, résumée par année. Aucun produit n'est recommandé.
PlanResult simulate(const PlanInput& input);

}  // namespace vigie::plan
