require 'redis'

redis = Redis.new

redis.gvertex 'graph1', 'a', 'b', 'c'
redis.gedge 'graph1', 'a', 'b', 1
redis.gedge 'graph1', 'a', 'b', 3
redis.gedge 'graph1', 'b', 'c', 2

result = redis.gshortestpath('graph1', 'a', 'c')

describe 'shortest path' do
  it 'should make the path from a to c = 5' do
    result.last.should eq "5"
  end
end

