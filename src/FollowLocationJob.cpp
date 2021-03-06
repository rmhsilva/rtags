/* This file is part of RTags (http://rtags.net).

   RTags is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   RTags is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with RTags.  If not, see <http://www.gnu.org/licenses/>. */

#include "FollowLocationJob.h"

#include "Project.h"
#include "rct/SignalSlot.h"
#include "RTags.h"
#include "Server.h"

FollowLocationJob::FollowLocationJob(Location loc,
                                     const std::shared_ptr<QueryMessage> &query,
                                     const std::shared_ptr<Project> &project)
    : QueryJob(query, project), location(loc)
{
}

int FollowLocationJob::execute()
{
    int idx = 0;
    Symbol symbol = project()->findSymbol(location, &idx);
    if (symbol.isNull()) {
        return 1;
    }

    // if you invoke a destructor explicitly there's a typeref on the class
    // name. This finds the destructor instead.
    if ((symbol.kind == CXCursor_TypeRef || symbol.kind == CXCursor_TemplateRef) && idx > 0) {
        auto symbols = project()->openSymbols(location.fileId());
        if (!symbols || !symbols->count())
            return 1;
        const Symbol prev = symbols->valueAt(idx - 1);
        if (prev.kind == CXCursor_MemberRefExpr
            && prev.location.column() == symbol.location.column() - 1
            && prev.location.line() == symbol.location.line()
            && prev.symbolName.contains("~")) {
            symbol = prev;
        }
    }

    if (queryFlags() & QueryMessage::TargetUsrs) {
        const Set<String> usrs = project()->findTargetUsrs(location);
        for (const String &usr : usrs) {
            for (const Symbol &s : project()->findByUsr(usr, location.fileId(), Project::ArgDependsOn, location)) {
                write(s.toString());
            }
        }
        return 0;
    }

    auto targets = project()->findTargets(symbol).toList();
    targets.sort([](const Symbol &l, const Symbol &r) {
            const int lrank = RTags::targetRank(l.kind);
            const int rrank = RTags::targetRank(r.kind);
            if (lrank != rrank)
                return lrank > rrank;
            if (l.isDefinition() != r.isDefinition())
                return l.isDefinition();
            return l.location < r.location;
        });

    int rank = -1;
    Set<Location> seen;
    auto writeTarget = [&rank, this, &seen](const Symbol &target) {
        if (seen.insert(target.location)) {
            write(target.location);
            rank = RTags::targetRank(target.kind);
        }
    };
    for (const auto &target : targets) {
        if (target.location.isNull())
            continue;

        if (symbol.usr == target.usr) {
            writeTarget(target);
            if (queryFlags() & QueryMessage::AllTargets)
                continue;
            break;
        }

        if (queryFlags() & QueryMessage::DeclarationOnly ? target.isDefinition() : !target.isDefinition()) {
            const auto other = project()->findTarget(target);
            if (!other.isNull() && other.usr == target.usr) {
                writeTarget(other);
                if (queryFlags() & QueryMessage::AllTargets)
                    continue;
                break;
            }
        }
        writeTarget(target);
        if (!(queryFlags() & QueryMessage::AllTargets))
            break;
    }
    return 0;
}
