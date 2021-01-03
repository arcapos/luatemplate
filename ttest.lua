local template = require 'template'

local msg = {}
local function collect(text)
	msg[#msg + 1] = text
end

-- create a new rendering context
local ctx = template.context()

data = {
	title = 'Hello, world!',
	b = 42,
}

-- ctx:debug(true)
ctx:renderFile(arg[1] or 'sample.lt', data, collect)
print(table.concat(msg))

-- Change data
data.title = 'Hello, world, again!'

-- render a second time
ctx:renderFile(arg[1] or 'sample.lt', data)
