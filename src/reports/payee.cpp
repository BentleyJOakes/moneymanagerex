/*******************************************************
Copyright (C) 2006-2012

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
********************************************************/

#include "payee.h"

#include "htmlbuilder.h"
#include "mmgraphpie.h"
#include "model/Model_Currency.h"
#include "model/Model_Payee.h"
#include "model/Model_Account.h"

#include <algorithm>

#define PAYEE_SORT_BY_NAME      1
#define PAYEE_SORT_BY_INCOME    2
#define PAYEE_SORT_BY_EXPENSE   3
#define PAYEE_SORT_BY_DIFF      4

mmReportPayeeExpenses::mmReportPayeeExpenses(const wxString& title, mmDateRange* date_range)
: mmPrintableBase(PAYEE_SORT_BY_NAME)
    , title_(title)
    , date_range_(date_range)
    , positiveTotal_(0.0)
    , negativeTotal_(0.0)
{
}

mmReportPayeeExpenses::~mmReportPayeeExpenses()
{
    if (date_range_) delete date_range_;
}

void  mmReportPayeeExpenses::RefreshData()
{
    data_.clear();
    valueList_.clear();
    positiveTotal_ = 0.0;
    negativeTotal_ = 0.0;

    std::map<int, std::pair<double, double> > payeeStats;
    getPayeeStats(payeeStats, date_range_
        , mmIniOptions::instance().ignoreFutureTransactions_);

    data_holder line;
    for (const auto& entry : payeeStats)
    {
        positiveTotal_ += entry.second.first;
        negativeTotal_ += entry.second.second;

        Model_Payee::Data* payee = Model_Payee::instance().get(entry.first);
        if (payee)
            line.name = payee->PAYEENAME;
        line.incomes = entry.second.first;
        line.expenses = entry.second.second;
        data_.push_back(line);

        if (line.incomes + line.expenses < 0)
        {
            ValuePair vp;
            vp.label = line.name;
            vp.amount = line.incomes + line.expenses;
            valueList_.push_back(vp);
        }
    }
}

wxString mmReportPayeeExpenses::getHTMLText()
{
    switch (sortColumn_)
    {
    case PAYEE_SORT_BY_NAME:
        std::stable_sort(data_.begin(), data_.end()
            , [] (const data_holder& x, const data_holder& y)
            {
                return x.name < y.name;
            }
        );
        break;
    case PAYEE_SORT_BY_INCOME:
        std::stable_sort(data_.begin(), data_.end()
            , [] (const data_holder& x, const data_holder& y)
            {
                if (x.incomes != y.incomes) return x.incomes < y.incomes;
                else return x.name < y.name;
            }
        );
        break;
    case PAYEE_SORT_BY_EXPENSE:
        std::stable_sort(data_.begin(), data_.end()
            , [] (const data_holder& x, const data_holder& y)
            {
                if (x.expenses != y.expenses) return x.expenses < y.expenses;
                else return x.name < y.name;
            }
        );
        break;
    default:
        sortColumn_ = PAYEE_SORT_BY_DIFF;
        std::stable_sort(data_.begin(), data_.end()
            , [] (const data_holder& x, const data_holder& y)
            {
                if (x.expenses+x.incomes != y.expenses+y.incomes) return x.expenses+x.incomes < y.expenses+y.incomes;
                else return x.name < y.name;
            }
        );
    }

    mmHTMLBuilder hb;
    hb.init();
    hb.addDivContainer();
    hb.addHeader(2, title_);
    hb.DisplayDateHeading(date_range_->start_date(), date_range_->end_date(), date_range_->is_with_date());

    hb.addDivRow();
    hb.addDivCol8();
    // Add the graph
    mmGraphPie gg;
    hb.addImage(gg.getOutputFileName());

    hb.startSortTable();
    hb.startThead();
    hb.startTableRow();
        hb.addTableHeaderCell(_("Payee"));
        hb.addTableHeaderCell(_("Incomes"), true);
        hb.addTableHeaderCell(_("Expenses"), true);
        hb.addTableHeaderCell(_("Difference"), true);
    hb.endTableRow();
    hb.endThead();

    hb.startTbody();
    for (const auto& entry : data_)
    {
        hb.startTableRow();
        hb.addTableCell(entry.name);
        hb.addMoneyCell(entry.incomes);
        hb.addMoneyCell(entry.expenses);
        hb.addMoneyCell(entry.incomes + entry.expenses);
        hb.endTableRow();
    }
    hb.endTbody();

    hb.startTfoot();
    std::vector <double> totals;
    totals.push_back(positiveTotal_);
    totals.push_back(negativeTotal_);
    totals.push_back(positiveTotal_ + negativeTotal_);
    hb.addTotalRow(_("Total:"), 3, totals);
    hb.endTfoot();

    hb.endTable();
    hb.endDiv();
    hb.endDiv();
    hb.endDiv();
    hb.end();

    gg.init(valueList_);
    gg.Generate(title_);

    return hb.getHTMLText();
}

void mmReportPayeeExpenses::getPayeeStats(std::map<int, std::pair<double, double> > &payeeStats
                                          , mmDateRange* date_range, bool ignoreFuture) const
{
    //Get base currency rates for all accounts
    std::map<int, double> acc_conv_rates;
    for (const auto& account: Model_Account::instance().all())
    {
        Model_Currency::Data* currency = Model_Account::currency(account);
        acc_conv_rates[account.ACCOUNTID] = currency->BASECONVRATE;
    }

    const auto &transactions = Model_Checking::instance().all();
    const auto all_splits = Model_Splittransaction::instance().get_all();
    for (const auto & trx: transactions)
    {
        if (Model_Checking::status(trx) == Model_Checking::VOID_) continue;
        if (Model_Checking::type(trx) == Model_Checking::TRANSFER) continue;

        wxDateTime trx_date = Model_Checking::TRANSDATE(trx);
        if (ignoreFuture)
        {
            if (trx_date.IsLaterThan(wxDateTime::Today()))
                continue; //skip future dated transactions
        }

        if (!trx_date.IsBetween(date_range->start_date(), date_range->end_date()))
            continue; //skip

        double convRate = acc_conv_rates[trx.ACCOUNTID];

        Model_Splittransaction::Data_Set splits;
        if (all_splits.count(trx.id())) splits = all_splits.at(trx.id());
        if (splits.empty())
        {
            if (Model_Checking::type(trx) == Model_Checking::DEPOSIT)
                payeeStats[trx.PAYEEID].first += trx.TRANSAMOUNT * convRate;
            else
                payeeStats[trx.PAYEEID].second -= trx.TRANSAMOUNT * convRate;
        }
        else
        {
            for (const auto& entry : splits)
            {
                if (Model_Checking::type(trx) == Model_Checking::DEPOSIT)
                {
                    if (entry.SPLITTRANSAMOUNT >= 0)
                        payeeStats[trx.PAYEEID].first += entry.SPLITTRANSAMOUNT * convRate;
                    else
                        payeeStats[trx.PAYEEID].second += entry.SPLITTRANSAMOUNT * convRate;
                }
                else
                {
                    if (entry.SPLITTRANSAMOUNT < 0)
                        payeeStats[trx.PAYEEID].first -= entry.SPLITTRANSAMOUNT * convRate;
                    else
                        payeeStats[trx.PAYEEID].second -= entry.SPLITTRANSAMOUNT * convRate;
                }
            }
        }
    }
}
