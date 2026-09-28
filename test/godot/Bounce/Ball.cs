using Godot;

// Counts contacts through body_entered / body_exited (wired here, in _Ready).
public partial class Ball : RigidBody2D
{
    private int _enters;
    private int _exits;
    private int _ticks;

    public override void _Ready()
    {
        BodyEntered += OnBodyEntered;
        BodyExited += OnBodyExited;
    }

    private void OnBodyEntered(Node body)
    {
        _enters = _enters + 1;
    }

    private void OnBodyExited(Node body)
    {
        _exits = _exits + 1;
    }

    public override void _PhysicsProcess(double delta)
    {
        _ticks = _ticks + 1;
        if (_ticks == 180)
        {
            GD.Print("contacts enters=", _enters, " exits=", _exits);
        }
    }
}
