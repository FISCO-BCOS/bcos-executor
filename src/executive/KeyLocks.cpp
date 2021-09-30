#include "KeyLocks.h"
#include <assert.h>

using namespace bcos::executor;

bool KeyLocks::acquireKeyLock(const std::string_view& key, int contextID)
{
    assert(contextID >= 0);

    auto it = m_key2ContextID.find(key);
    if (it != m_key2ContextID.end())
    {
        if (it->second == contextID || it->second < 0)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    m_key2ContextID.emplace(key, contextID);
    return true;
}

void KeyLocks::releaseKeyLocks(int contextID) {
}