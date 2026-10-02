/*
 * mod-playerbot-raid-finder — RfInternalOp.h
 *
 * RAII guard for group/teleport operations the module performs itself. While a
 * guard is alive, the group hooks ignore the events those operations raise
 * (OnRemoveMember, OnDisband, ...), so cleanup never re-enters cleanup.
 */

#ifndef MOD_RF_INTERNAL_OP_H
#define MOD_RF_INTERNAL_OP_H

namespace RaidFinder
{
    class RfInternalOp
    {
    public:
        RfInternalOp() { ++Depth(); }
        ~RfInternalOp() { --Depth(); }
        RfInternalOp(RfInternalOp const&) = delete;
        RfInternalOp& operator=(RfInternalOp const&) = delete;

        static bool Active() { return Depth() > 0; }

    private:
        static int& Depth()
        {
            thread_local int depth = 0;
            return depth;
        }
    };
}

#endif
