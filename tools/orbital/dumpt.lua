local function dump(v, ind, depth)
  ind = ind or ''; depth = depth or 0
  if type(v) ~= 'table' then return (type(v)=='string') and string.format('%q', v) or tostring(v) end
  if depth > 3 then return '{...}' end
  local keys = {}
  for k in pairs(v) do keys[#keys+1] = k end
  table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
  local out = {'{'}
  for _, k in ipairs(keys) do out[#out+1] = ind .. '  ' .. tostring(k) .. ' = ' .. dump(v[k], ind .. '  ', depth + 1) .. ',' end
  out[#out+1] = ind .. '}'
  return table.concat(out, '\n')
end
_G.dump = dump
