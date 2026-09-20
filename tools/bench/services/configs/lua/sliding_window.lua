local key = KEYS[1]
local limit = tonumber(ARGV[1])
local window_ms = tonumber(ARGV[2])
local now = tonumber(ARGV[3])
local cost = tonumber(ARGV[4] or 1)

local clear_before = now - window_ms
redis.call('ZREMRANGEBYSCORE', key, 0, clear_before)

local current_requests = redis.call('ZCARD', key)
if current_requests + cost <= limit then
    for i = 1, cost do
        redis.call('ZADD', key, now, now .. '-' .. i .. '-' .. math.random(10000))
    end
    redis.call('PEXPIRE', key, window_ms)
    return {1, limit - (current_requests + cost)}
else
    return {0, 0}
end
