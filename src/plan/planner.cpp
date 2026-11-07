#include "plan/planner.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vigie::plan {
namespace {

constexpr int kMaximumHorizon = 50;
constexpr double kMaximumAmount = 1e9;
constexpr std::size_t kMaximumGoals = 10;

void check_amount(double value, const char* what) {
    if (!std::isfinite(value) || value < 0.0 || value > kMaximumAmount) {
        throw std::invalid_argument(std::string(what) + " doit être un montant entre 0 et 1 milliard.");
    }
}

void check_rate(double value, double low, double high, const char* what) {
    if (!std::isfinite(value) || value < low || value > high) {
        throw std::invalid_argument(std::string(what) + " sort des bornes prévues.");
    }
}

double monthly_rate(double annual_rate) { return std::pow(1.0 + annual_rate, 1.0 / 12.0) - 1.0; }

struct Run {
    std::vector<double> year_end;            // Capital en fin d'année.
    std::vector<std::vector<std::string>> paid;
    std::vector<double> shortfall;           // Par objectif.
};

// Un seul compte : chaque objectif est payé à son échéance s'il reste assez d'argent.
Run project(const PlanInput& input, double monthly_saving, double annual_rate) {
    Run run;
    run.shortfall.assign(input.goals.size(), 0.0);
    const double rate = monthly_rate(annual_rate);
    double capital = input.current_savings;
    double saving = monthly_saving;

    for (int year = 1; year <= input.horizon_years; ++year) {
        for (int month = 0; month < 12; ++month) {
            capital = capital * (1.0 + rate) + saving;
        }
        std::vector<std::string> paid;
        for (std::size_t index = 0; index < input.goals.size(); ++index) {
            const Goal& goal = input.goals[index];
            if (goal.in_years != year) {
                continue;
            }
            const double cost = goal.amount * std::pow(1.0 + input.inflation, year);
            if (capital + 1e-6 >= cost) {
                capital -= cost;
                paid.push_back(goal.name);
            } else {
                run.shortfall[index] = cost - capital;
            }
        }
        run.year_end.push_back(capital);
        run.paid.push_back(std::move(paid));
        saving *= 1.0 + input.saving_growth;
    }
    return run;
}

bool all_reached(const Run& run) {
    return std::all_of(run.shortfall.begin(), run.shortfall.end(), [](double value) { return value <= 0.0; });
}

}  // namespace

void validate(const PlanInput& input) {
    if (input.horizon_years < 1 || input.horizon_years > kMaximumHorizon) {
        throw std::invalid_argument("L'horizon doit être compris entre 1 et 50 ans.");
    }
    if (input.age && (*input.age < 0 || *input.age > 120)) {
        throw std::invalid_argument("L'âge doit être compris entre 0 et 120 ans.");
    }
    check_amount(input.monthly_income, "Le revenu mensuel");
    check_amount(input.monthly_expenses, "Les dépenses mensuelles");
    check_amount(input.current_savings, "L'épargne actuelle");
    if (input.monthly_saving) {
        check_amount(*input.monthly_saving, "L'épargne mensuelle");
    }
    check_rate(input.saving_growth, -0.5, 0.5, "La hausse annuelle de l'épargne");
    check_rate(input.inflation, -0.05, 0.2, "L'inflation");
    if (input.emergency_months < 0 || input.emergency_months > 24) {
        throw std::invalid_argument("L'épargne de précaution doit couvrir entre 0 et 24 mois.");
    }
    if (input.scenarios.empty() || input.scenarios.size() > 5) {
        throw std::invalid_argument("Il faut entre 1 et 5 hypothèses de rendement.");
    }
    for (const Scenario& scenario : input.scenarios) {
        check_rate(scenario.annual_rate, -0.5, 0.5, "Un rendement annuel");
    }
    if (input.goals.size() > kMaximumGoals) {
        throw std::invalid_argument("Le plan accepte au plus 10 objectifs.");
    }
    for (const Goal& goal : input.goals) {
        check_amount(goal.amount, "Le montant d'un objectif");
        if (goal.in_years < 1 || goal.in_years > kMaximumHorizon) {
            throw std::invalid_argument("L'échéance d'un objectif doit être comprise entre 1 et 50 ans.");
        }
    }
}

PlanResult simulate(const PlanInput& input) {
    validate(input);

    PlanResult result;
    result.scenarios = input.scenarios;
    result.central = input.scenarios.size() / 2;
    result.monthly_capacity = input.monthly_income - input.monthly_expenses;
    result.monthly_saving = input.monthly_saving.value_or(std::max(0.0, result.monthly_capacity));
    result.saving_rate = input.monthly_income > 0.0 ? result.monthly_saving / input.monthly_income : 0.0;
    result.emergency_target = input.emergency_months * input.monthly_expenses;

    if (result.monthly_capacity < 0.0) {
        result.warnings.push_back("Tes dépenses dépassent tes revenus : le plan suppose que tu épargnes quand même.");
    } else if (result.monthly_saving > result.monthly_capacity + 1e-6) {
        result.warnings.push_back("L'épargne mensuelle choisie dépasse la différence entre tes revenus et tes dépenses.");
    }

    const double gap = result.emergency_target - input.current_savings;
    if (gap <= 0.0) {
        result.months_to_emergency = 0;
    } else if (result.monthly_saving > 0.0) {
        result.months_to_emergency = static_cast<int>(std::ceil(gap / result.monthly_saving));
    }

    std::vector<Run> runs;
    for (const Scenario& scenario : input.scenarios) {
        runs.push_back(project(input, result.monthly_saving, scenario.annual_rate));
    }
    const Run& central = runs[result.central];

    double contributed = input.current_savings;
    double saving = result.monthly_saving;
    for (int year = 1; year <= input.horizon_years; ++year) {
        contributed += 12.0 * saving;
        saving *= 1.0 + input.saving_growth;

        YearRow row;
        row.year = year;
        if (input.age) {
            row.age = *input.age + year;
        }
        row.contributed = contributed;
        for (const Run& run : runs) {
            row.capital.push_back(run.year_end[static_cast<std::size_t>(year - 1)]);
        }
        row.central_in_todays_euros = central.year_end[static_cast<std::size_t>(year - 1)] /
                                      std::pow(1.0 + input.inflation, year);
        row.goals_paid = central.paid[static_cast<std::size_t>(year - 1)];
        result.years.push_back(std::move(row));
    }

    for (std::size_t index = 0; index < input.goals.size(); ++index) {
        const Goal& goal = input.goals[index];
        GoalResult item;
        item.name = goal.name;
        item.in_years = goal.in_years;
        item.amount_today = goal.amount;
        item.amount_then = goal.amount * std::pow(1.0 + input.inflation, goal.in_years);
        if (goal.in_years > input.horizon_years) {
            result.warnings.push_back("L'objectif « " + goal.name + " » dépasse l'horizon du plan.");
        } else {
            item.reached = central.shortfall[index] <= 0.0;
            item.shortfall = central.shortfall[index];
        }
        result.goals.push_back(std::move(item));
    }

    // Épargne mensuelle minimale qui finance tous les objectifs, par dichotomie.
    if (!input.goals.empty()) {
        const double rate = input.scenarios[result.central].annual_rate;
        double low = 0.0;
        double high = 1000.0;
        while (high < kMaximumAmount && !all_reached(project(input, high, rate))) {
            high *= 2.0;
        }
        if (high < kMaximumAmount) {
            for (int step = 0; step < 60; ++step) {
                const double middle = (low + high) / 2.0;
                (all_reached(project(input, middle, rate)) ? high : low) = middle;
            }
            result.monthly_saving_for_all_goals = std::ceil(high);
        }
    }

    return result;
}

}  // namespace vigie::plan
