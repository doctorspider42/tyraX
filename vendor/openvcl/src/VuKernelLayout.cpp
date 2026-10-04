#include "VuKernelLayout.h"

#include <map>

namespace vcl
{

namespace
{
    struct RegPlanAcc { unsigned int entry; unsigned int stage; int kind; };
}

bool vuKernelLayoutEntryLess( const VuKernelLayoutEntry& a,
                              const VuKernelLayoutEntry& b )
{
    if( a.stage != b.stage )       return a.stage   < b.stage;
    if( a.modSlot != b.modSlot )   return a.modSlot < b.modSlot;
    return a.nodeIndex < b.nodeIndex;
}

void buildVuKernelTemplate( const VuKernelLayout& layout,
                            VuKernelTemplate& template_ )
{
    template_.II        = layout.II;
    template_.conflicts = 0;
    template_.slots.clear();
    if( layout.II == 0 ) return;
    template_.slots.resize( layout.II );

    for( unsigned int i = 0; i < layout.entries.size(); ++i )
    {
        const VuKernelLayoutEntry& ent = layout.entries[i];
        if( ent.modSlot >= layout.II ) { ++template_.conflicts; continue; }
        VuKernelTemplateSlot& s = template_.slots[ent.modSlot];
        int* lane = NULL;
        switch( ent.pipe )
        {
        case 1: lane = &s.upper; break;
        case 2: lane = &s.lower; break;
        case 3: lane = &s.fdiv;  break;
        case 4: lane = &s.efu;   break;
        default: continue; // pipe=0 (none) is invisible to the template
        }
        if( *lane != VuKernelTemplateSlot::NO_ENTRY ) ++template_.conflicts;
        *lane = (int)i;
    }
}

void buildVuKernelEnvelope( const VuKernelLayout& layout,
                            VuKernelEnvelope& envelope )
{
    envelope.II             = layout.II;
    envelope.stageCount     = layout.stageCount;
    envelope.kernelTokens   = (unsigned int)layout.entries.size();
    envelope.prologueCycles = 0;
    envelope.epilogueCycles = 0;
    envelope.conflicts      = 0;
    envelope.prologueTokenCounts.clear();
    envelope.epilogueTokenCounts.clear();
    envelope.prologueRows.clear();
    envelope.epilogueRows.clear();
    if( layout.stageCount < 2 || layout.II == 0 ) return;

    const unsigned int copies = layout.stageCount - 1;
    envelope.prologueCycles = copies * layout.II;
    envelope.epilogueCycles = copies * layout.II;
    envelope.prologueTokenCounts.resize( copies, 0 );
    envelope.epilogueTokenCounts.resize( copies, 0 );
    envelope.prologueRows.resize( copies * layout.II );
    envelope.epilogueRows.resize( copies * layout.II );

    for( unsigned int i = 0; i < layout.entries.size(); ++i )
    {
        const VuKernelLayoutEntry& e = layout.entries[i];
        const unsigned int st  = e.stage;
        const unsigned int ms  = e.modSlot;
        const int          pp  = e.pipe;
        if( ms >= layout.II ) continue;

        // Prologue copies p in [st, copies-1]; stages [0,p] active.
        for( unsigned int p = st; p < copies; ++p )
        {
            ++envelope.prologueTokenCounts[p];
            if( pp == 0 ) continue;
            VuKernelTemplateSlot& row = envelope.prologueRows[p * layout.II + ms];
            int* lane = NULL;
            if     ( pp == 1 ) lane = &row.upper;
            else if( pp == 2 ) lane = &row.lower;
            else if( pp == 3 ) lane = &row.fdiv;
            else if( pp == 4 ) lane = &row.efu;
            else continue;
            if( *lane != VuKernelTemplateSlot::NO_ENTRY ) ++envelope.conflicts;
            *lane = (int)i;
        }

        // Epilogue copies q in [1, min(st,copies)]; stages [q,S-1] active.
        const unsigned int qhi = ( st < copies ) ? st : copies;
        for( unsigned int q = 1; q <= qhi; ++q )
        {
            ++envelope.epilogueTokenCounts[q - 1];
            if( pp == 0 ) continue;
            VuKernelTemplateSlot& row = envelope.epilogueRows[(q - 1) * layout.II + ms];
            int* lane = NULL;
            if     ( pp == 1 ) lane = &row.upper;
            else if( pp == 2 ) lane = &row.lower;
            else if( pp == 3 ) lane = &row.fdiv;
            else if( pp == 4 ) lane = &row.efu;
            else continue;
            if( *lane != VuKernelTemplateSlot::NO_ENTRY ) ++envelope.conflicts;
            *lane = (int)i;
        }
    }
}

void buildVuKernelRegisterPlan(
    const VuKernelLayout& layout,
    const std::vector< VuKernelEntryRegisters >& entryRegs,
    VuKernelRegisterPlan& plan )
{
    plan = VuKernelRegisterPlan();

    // Per-register accesses: (entry index, stage, kind).
    std::map< std::string, std::vector< RegPlanAcc > > byReg;

    const unsigned int n = (unsigned int)layout.entries.size();
    const unsigned int m = (unsigned int)entryRegs.size();
    const unsigned int lim = ( n < m ) ? n : m;

    for( unsigned int i = 0; i < lim; ++i )
    {
        const VuKernelLayoutEntry& e = layout.entries[i];
        const VuKernelEntryRegisters& r = entryRegs[i];
        for( unsigned int k = 0; k < r.reads.size(); ++k )
        {
            RegPlanAcc a; a.entry = i; a.stage = e.stage; a.kind = 0;
            byReg[r.reads[k]].push_back( a );
        }
        for( unsigned int k = 0; k < r.writes.size(); ++k )
        {
            RegPlanAcc a; a.entry = i; a.stage = e.stage; a.kind = 1;
            byReg[r.writes[k]].push_back( a );
        }
    }

    plan.regCount = (unsigned int)byReg.size();

    typedef std::map< std::string, std::vector< RegPlanAcc > >::const_iterator MapIt;
    for( MapIt it = byReg.begin(); it != byReg.end(); ++it )
    {
        const std::vector< RegPlanAcc >& v = it->second;
        for( unsigned int a = 0; a < v.size(); ++a )
        {
            for( unsigned int b = a + 1; b < v.size(); ++b )
            {
                if( v[a].stage == v[b].stage ) continue;
                if( v[a].kind == 0 && v[b].kind == 0 ) continue;
                VuKernelRegisterHazard h;
                h.reg    = it->first;
                h.entryA = v[a].entry;
                h.entryB = v[b].entry;
                h.stageA = v[a].stage;
                h.stageB = v[b].stage;
                h.kindA  = v[a].kind;
                h.kindB  = v[b].kind;
                plan.hazards.push_back( h );
                if( v[a].kind == 1 && v[b].kind == 1 ) ++plan.wawCount;
                else if( v[a].kind == 0 && v[b].kind == 1 ) ++plan.warCount;
                else if( v[a].kind == 1 && v[b].kind == 0 ) ++plan.rawCount;
            }
        }
    }
}

namespace
{
    unsigned int rewriteLaneToken( const VuKernelLayout& layout, int slotEntry )
    {
        if( slotEntry == VuKernelTemplateSlot::NO_ENTRY )
            return VuKernelRewritePlan::NO_TOKEN;
        return layout.entries[ static_cast< unsigned int >( slotEntry ) ].tokenIndex;
    }

    void rewritePushCycle( const VuKernelLayout& layout,
                           const VuKernelTemplateSlot& slot,
                           std::vector< unsigned int >& dst )
    {
        dst.push_back( rewriteLaneToken( layout, slot.upper ) );
        dst.push_back( rewriteLaneToken( layout, slot.lower ) );
        dst.push_back( rewriteLaneToken( layout, slot.fdiv  ) );
        dst.push_back( rewriteLaneToken( layout, slot.efu   ) );
    }
}

void buildVuKernelRewritePlan( const VuKernelLayout& layout,
                               const VuKernelEnvelope& envelope,
                               VuKernelRewritePlan& plan )
{
    plan.II         = envelope.II;
    plan.stageCount = envelope.stageCount;
    plan.conflicts  = envelope.conflicts;
    plan.prologTokens.clear();
    plan.mainTokens.clear();
    plan.drainTokens.clear();
    plan.entryStages.clear();
    plan.stageCells.clear();

    if( envelope.stageCount == 0 || envelope.II == 0 ) return;

    // Per-entry stage assignments.
    plan.entryStages.reserve( layout.entries.size() );
    for( unsigned int e = 0; e < layout.entries.size(); ++e )
        plan.entryStages.push_back( layout.entries[ e ].stage );

    // (stage, modSlot) grid: place each entry into its (stage, modSlot, pipe) lane.
    plan.stageCells.resize( envelope.stageCount * envelope.II );
    for( unsigned int e = 0; e < layout.entries.size(); ++e )
    {
        const VuKernelLayoutEntry& ent = layout.entries[ e ];
        if( ent.stage >= envelope.stageCount || ent.modSlot >= envelope.II )
            continue;
        VuKernelTemplateSlot& cell = plan.stageCells[ ent.stage * envelope.II + ent.modSlot ];
        const int ei = static_cast< int >( e );
        switch( ent.pipe )
        {
        case 1: if( cell.upper == VuKernelTemplateSlot::NO_ENTRY ) cell.upper = ei; else ++plan.conflicts; break;
        case 2: if( cell.lower == VuKernelTemplateSlot::NO_ENTRY ) cell.lower = ei; else ++plan.conflicts; break;
        case 3: if( cell.fdiv  == VuKernelTemplateSlot::NO_ENTRY ) cell.fdiv  = ei; else ++plan.conflicts; break;
        case 4: if( cell.efu   == VuKernelTemplateSlot::NO_ENTRY ) cell.efu   = ei; else ++plan.conflicts; break;
        default: break;
        }
    }

    VuKernelTemplate tmpl;
    buildVuKernelTemplate( layout, tmpl );
    plan.mainTokens.reserve( envelope.II * 4 );
    for( unsigned int c = 0; c < envelope.II; ++c )
        rewritePushCycle( layout, tmpl.slots[ c ], plan.mainTokens );

    if( envelope.stageCount <= 1 ) return;

    const unsigned int copies = envelope.stageCount - 1;
    plan.prologTokens.reserve( copies * envelope.II * 4 );
    for( unsigned int p = 0; p < copies; ++p )
        for( unsigned int c = 0; c < envelope.II; ++c )
            rewritePushCycle( layout, envelope.prologueRows[ p * envelope.II + c ],
                              plan.prologTokens );

    plan.drainTokens.reserve( copies * envelope.II * 4 );
    for( unsigned int q = 1; q <= copies; ++q )
        for( unsigned int c = 0; c < envelope.II; ++c )
            rewritePushCycle( layout, envelope.epilogueRows[ ( q - 1 ) * envelope.II + c ],
                              plan.drainTokens );
}

void buildVuKernelRenameHints( const VuKernelRegisterPlan& regPlan,
                               std::vector< VuKernelRenameHint >& hints )
{
    hints.clear();
    // Deduplicate by (entry, reg, kind). Use a simple linear-scan
    // dedup; hazard counts in practice are small (single-digit per loop).
    for( unsigned int h = 0; h < regPlan.hazards.size(); ++h )
    {
        const VuKernelRegisterHazard& hz = regPlan.hazards[ h ];
        for( unsigned int side = 0; side < 2; ++side )
        {
            VuKernelRenameHint hint;
            hint.reg   = hz.reg;
            hint.entry = ( side == 0 ) ? hz.entryA : hz.entryB;
            hint.stage = ( side == 0 ) ? hz.stageA : hz.stageB;
            hint.kind  = ( side == 0 ) ? hz.kindA  : hz.kindB;
            bool present = false;
            for( unsigned int e = 0; e < hints.size(); ++e )
            {
                if( hints[ e ].entry == hint.entry
                 && hints[ e ].kind  == hint.kind
                 && hints[ e ].reg   == hint.reg )
                {
                    present = true;
                    break;
                }
            }
            if( !present ) hints.push_back( hint );
        }
    }
}

namespace
{
    std::string vfBaseNameForScratch( unsigned int index )
    {
        std::string s = "VF";
        if( index < 10 ) s += '0';
        char buf[8];
        unsigned int n = index, len = 0;
        char tmp[8];
        if( n == 0 ) { tmp[ len++ ] = '0'; }
        else { while( n ) { tmp[ len++ ] = (char)( '0' + ( n % 10 ) ); n /= 10; } }
        for( unsigned int j = 0; j < len; ++j ) buf[ j ] = tmp[ len - 1 - j ];
        buf[ len ] = '\0';
        s += buf;
        return s;
    }

    std::string baseOfRegKey( const std::string& key )
    {
        // Strip ".x" / ".xyz" / etc. suffix, leave "VFnn".
        const std::string::size_type dot = key.find( '.' );
        if( dot == std::string::npos ) return key;
        return key.substr( 0, dot );
    }
}

void buildVuKernelRenameDecisions( const std::vector< VuKernelRenameHint >& hints,
                                   const std::vector< std::string >& usedBases,
                                   std::vector< VuKernelRenameDecision >& decisions )
{
    decisions.clear();

    // Unique regs from hints, in first-seen order.
    std::vector< std::string > uniqueRegs;
    for( unsigned int h = 0; h < hints.size(); ++h )
    {
        const std::string& r = hints[ h ].reg;
        bool present = false;
        for( unsigned int u = 0; u < uniqueRegs.size(); ++u )
            if( uniqueRegs[ u ] == r ) { present = true; break; }
        if( !present ) uniqueRegs.push_back( r );
    }

    // Reserved set: every base the loop body touches plus every scratch
    // already chosen by an earlier decision in this call.
    std::vector< std::string > reserved;
    for( unsigned int i = 0; i < usedBases.size(); ++i )
        reserved.push_back( baseOfRegKey( usedBases[ i ] ) );

    for( unsigned int u = 0; u < uniqueRegs.size(); ++u )
    {
        VuKernelRenameDecision d;
        d.reg = uniqueRegs[ u ];
        d.assigned = false;
        // Scan VF31..VF02; VF00 and VF01 are reserved by VU convention.
        for( unsigned int reverse = 32; reverse > 2; --reverse )
        {
            const std::string cand = vfBaseNameForScratch( reverse - 1 );
            bool collides = false;
            for( unsigned int r = 0; r < reserved.size(); ++r )
                if( reserved[ r ] == cand ) { collides = true; break; }
            if( collides ) continue;
            d.scratch  = cand;
            d.assigned = true;
            reserved.push_back( cand );
            break;
        }
        decisions.push_back( d );
    }
}

void buildVuKernelRenameMoveSlots(
    const std::vector< VuKernelRenameDecision >& decisions,
    const VuKernelLayout& layout,
    const std::vector< VuKernelEntryRegisters >& entryRegs,
    const VuKernelTemplate& tmpl,
    std::vector< VuKernelRenameMoveSlot >& slots )
{
    slots.clear();
    const unsigned int II = tmpl.II;

    // Track which (modSlot, lane) cells earlier decisions in this call
    // have already claimed, so two MOVEs never collide.
    std::vector< unsigned char > claimedUpper( II, 0 );
    std::vector< unsigned char > claimedLower( II, 0 );

    for( unsigned int d = 0; d < decisions.size(); ++d )
    {
        VuKernelRenameMoveSlot s;
        s.decisionIndex = d;

        if( !decisions[ d ].assigned || II == 0u )
        {
            slots.push_back( s );
            continue;
        }

        // Collect the modSlots at which any entry *reads* this decision's
        // reg. The MOVE cannot be at any of these slots.
        std::vector< unsigned int > forbidden;
        for( unsigned int e = 0; e < layout.entries.size(); ++e )
        {
            if( e >= entryRegs.size() ) continue;
            const VuKernelEntryRegisters& er = entryRegs[ e ];
            bool reads = false;
            for( unsigned int k = 0; k < er.reads.size(); ++k )
            {
                if( baseOfRegKey( er.reads[ k ] ) == decisions[ d ].reg )
                { reads = true; break; }
            }
            if( !reads ) continue;
            const unsigned int ms = layout.entries[ e ].modSlot;
            bool present = false;
            for( unsigned int j = 0; j < forbidden.size(); ++j )
                if( forbidden[ j ] == ms ) { present = true; break; }
            if( !present ) forbidden.push_back( ms );
        }

        // Walk slots c = 0..II-1. Prefer upper, else lower.
        for( unsigned int c = 0; c < II && !s.assigned; ++c )
        {
            bool isForbidden = false;
            for( unsigned int j = 0; j < forbidden.size(); ++j )
                if( forbidden[ j ] == c ) { isForbidden = true; break; }
            if( isForbidden ) continue;
            if( c >= tmpl.slots.size() ) continue;
            if( claimedUpper[ c ] ) continue;
            const VuKernelTemplateSlot& cell = tmpl.slots[ c ];
            if( cell.upper == VuKernelTemplateSlot::NO_ENTRY )
            {
                s.modSlot       = c;
                s.lane          = 1;
                s.assigned      = true;
                claimedUpper[ c ] = 1;
                break;
            }
        }
        if( !s.assigned )
        {
            for( unsigned int c = 0; c < II && !s.assigned; ++c )
            {
                bool isForbidden = false;
                for( unsigned int j = 0; j < forbidden.size(); ++j )
                    if( forbidden[ j ] == c ) { isForbidden = true; break; }
                if( isForbidden ) continue;
                if( c >= tmpl.slots.size() ) continue;
                if( claimedLower[ c ] ) continue;
                const VuKernelTemplateSlot& cell = tmpl.slots[ c ];
                if( cell.lower == VuKernelTemplateSlot::NO_ENTRY )
                {
                    s.modSlot       = c;
                    s.lane          = 2;
                    s.assigned      = true;
                    claimedLower[ c ] = 1;
                    break;
                }
            }
        }
        slots.push_back( s );
    }
}

} // namespace vcl
