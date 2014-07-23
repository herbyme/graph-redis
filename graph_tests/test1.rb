require 'redis'

redis = Redis.new

redis.gvertex 'graph1', 'a', 'b', 'c'
redis.gedge 'graph1', 'a', 'b', 1
redis.gedge 'graph1', 'a', 'b', 3
redis.gedge 'graph1', 'b', 'c', 2

result = redis.gshortestpath('graph1', 'a', 'c')
result2 = redis.gshortestpath('graph1', 'a', 'c')

describe 'shortest path' do
  it 'should make the path from a to c = 5' do
    result.last.should eq "5"
  end

  it 'should have the right order of the path' do
    puts result.inspect
    puts result2.inspect

    result[0].should eq 'a'
    result[1].should eq 'b'
    result[2].should eq 'c'
  end

end

